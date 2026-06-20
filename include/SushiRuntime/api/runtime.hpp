/**************************************************************************/
/* runtime.hpp                                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/buffer.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    /**
     * @brief The fluent, public v1 API: a Runtime that hands out chainable graphs.
     *
     * This layer sits on top of RuntimeContext / TaskGraph / the execution engine
     * and is the surface users program against. It is header-only and template-
     * based, so it works identically across every supported SYCL toolchain.
     */
    namespace API
    {
        // The lower-level graph builder this layer wraps. Spelled out fully to
        // avoid clashing with the API::Graph class name below.
        using BackingGraph = ::SushiRuntime::Graph::TaskGraph;

        // The three callable shapes the fluent add() overloads accept, expressed
        // as C++17 SFINAE traits over std::is_invocable_v (the C++20 concepts they
        // replace read the same, only as `requires`-clauses). enable_if on each
        // add() selects exactly one. A wrong signature removes every overload, so
        // the compiler reports "no matching call" — less pointed than a concept
        // diagnostic but still naming the offending callable.

        /**
         * @brief A function callable once per element index: void(size_t).
         *
         * Submitted as a SYCL parallel_for over [0, n); the runtime picks the
         * device and the work-group mapping. Excludes the range-kernel shape so the
         * two add(size_t, F) overloads stay mutually exclusive.
         */
        template <class F>
        inline constexpr bool is_per_element_v =
            std::is_invocable_v<F, std::size_t> &&
            !std::is_invocable_v<F, std::size_t, std::size_t, sycl::handler&>;

        /**
         * @brief A raw kernel that drives a sycl::handler directly: void(sycl::handler&).
         */
        template <class F>
        inline constexpr bool is_raw_kernel_v =
            std::is_invocable_v<F, sycl::handler&>;

        /**
         * @brief A range kernel that receives its bounds and a handler:
         *        void(size_t begin, size_t end, sycl::handler&).
         */
        template <class F>
        inline constexpr bool is_range_kernel_v =
            std::is_invocable_v<F, std::size_t, std::size_t, sycl::handler&>;

        /**
         * @brief A chainable graph of tasks built fluently, then run.
         *
         * Every add() overload appends one task and returns *this so calls chain.
         * The concept-constrained overloads give a readable compiler error when a
         * callable has the wrong signature — something a macro cannot do.
         * Obtain one from Runtime::graph(); it is moveable but not copyable.
         */
        class Graph
        {
            public:
                Graph(Graph&&) noexcept = default;
                Graph& operator=(Graph&&) noexcept = default;
                Graph(const Graph&) = delete;
                Graph& operator=(const Graph&) = delete;

                /**
                 * @brief Adds a per-element task run as a parallel_for over [0, n).
                 * @param n  Number of elements (the iteration range).
                 * @param fn Callable invoked as fn(i) for each index i.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(std::size_t n, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), n](BackingGraph& bg)
                    {
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            h.parallel_for(sycl::range<1>(n),
                                           [fn](sycl::id<1> idx) { fn(idx[0]); });
                        });
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a range task; fn receives the bounds and the handler.
                 * @param n  Upper bound; fn is called as fn(0, n, handler).
                 * @param fn Callable invoked as fn(begin, end, sycl::handler&).
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                Graph& add(std::size_t n, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), n](BackingGraph& bg)
                    {
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            fn(std::size_t{0}, n, h);
                        });
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a raw kernel that drives the sycl::handler itself.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_raw_kernel_v<F>, int> = 0>
                Graph& add(F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn)](BackingGraph& bg)
                    {
                        bg.add_node(fn);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds the per-step update rule for a time-evolving State.
                 *
                 * This is the canonical stepped task: it reads the state's current
                 * field and writes its next field, with the dependency tracker
                 * keying on those two buffers automatically. The runtime supplies
                 * the current/next pointers to @p fn at execution time, so under
                 * run(n) the same compiled node follows the double-buffer swap with
                 * no recompilation. Provide exactly one update rule per State per
                 * step.
                 *
                 * @tparam T  Element type of the State.
                 * @param s   The state this rule evolves; must outlive the graph.
                 * @param fn  Callable invoked as fn(i, const T* cur, T* next) for
                 *            each index i in [0, s.size()).
                 * @return *this, for chaining.
                 */
                template <typename T, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                                                               const T*, T*>, int> = 0>
                Graph& add(State<T>& s, F&& fn)
                {
                    // The view is shared between the kernel (which reads it fresh at
                    // each submit, on the host side) and the advancer (which repoints
                    // it after each swap). It persists across recompiles so the
                    // double-buffer keeps following the same indirection.
                    auto view = std::make_shared<detail::StateView<T>>();
                    State<T>* sp = &s;

                    specs_.push_back([fn = std::forward<F>(fn), view, sp](BackingGraph& bg)
                    {
                        view->cur = sp->current();
                        view->next = sp->next();
                        const std::size_t n = sp->size();
                        bg.add_node([fn, view, n](sycl::handler& h)
                        {
                            // Read fresh each submit (each step) to follow the swap.
                            const T* cur = view->cur;
                            T* next = view->next;
                            h.parallel_for(sycl::range<1>(n),
                                           [=](sycl::id<1> i) { fn(i[0], cur, next); });
                        }, { sp->read_key() }, { sp->write_key() });
                    });

                    // Driven by run(n) between steps: advance the state, then point
                    // the view at the freshly swapped buffers.
                    step_advancers_.push_back([sp, view]()
                    {
                        sp->advance();
                        view->cur = sp->current();
                        view->next = sp->next();
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a per-element task with explicit read/write tracking.
                 *
                 * The advanced escape hatch for callers who manage their own data
                 * and want the dependency tracker to order this task against others
                 * by the pointers it touches. Prefer the State/Buffer overloads;
                 * reach for this only when handle-based tracking does not fit.
                 *
                 * @param reads  Base pointers this task reads (e.g. buffer keys).
                 * @param writes Base pointers this task writes.
                 * @param n      Iteration range; fn is called for each i in [0, n).
                 * @param fn     Callable invoked as fn(i) for each index i.
                 * @return *this, for chaining.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                Graph& add(const std::vector<void*>& reads,
                           const std::vector<void*>& writes,
                           std::size_t n, F&& fn)
                {
                    specs_.push_back([fn = std::forward<F>(fn), n, reads, writes](BackingGraph& bg)
                    {
                        bg.add_node([fn, n](sycl::handler& h)
                        {
                            h.parallel_for(sycl::range<1>(n),
                                           [fn](sycl::id<1> i) { fn(i[0]); });
                        }, reads, writes);
                    });
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /** @brief Number of update rules (tasks) added so far. */
                std::size_t size() const noexcept { return task_count_; }

                /**
                 * @brief Runs the graph once, blocking until it completes.
                 * @return A RunReport summarising the run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run() { return run(std::size_t{1}); }

                /**
                 * @brief Runs @p steps successive steps, blocking until done.
                 *
                 * The graph is compiled on the first run (and after any mutation),
                 * then the compiled plan is replayed for each step: node dependency
                 * state is reset, the plan re-executed, and every State advanced to
                 * its next field. Compilation happens at most once per call.
                 *
                 * @param steps Number of steps to run.
                 * @return A RunReport aggregating the whole run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run(std::size_t steps)
                {
                    return unwrap(try_run(steps));
                }

                /**
                 * @brief Runs steps until a wall-clock @p budget elapses.
                 * @param budget Wall-clock limit; at least one step always runs.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Rep, class Period>
                Core::RunReport run_for(std::chrono::duration<Rep, Period> budget)
                {
                    const auto deadline = std::chrono::steady_clock::now() + budget;
                    return unwrap(drive([deadline](std::size_t k)
                    {
                        return k == 0 || std::chrono::steady_clock::now() < deadline;
                    }));
                }

                /**
                 * @brief Runs steps until @p done returns true.
                 * @param done Stop condition checked before each step; the run ends
                 *             the first time it returns true.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Predicate>
                Core::RunReport run_until(Predicate done)
                {
                    return unwrap(drive([done](std::size_t) { return !done(); }));
                }

                /**
                 * @brief Non-throwing single run.
                 * @return The RunReport on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error> try_run() noexcept
                {
                    return try_run(std::size_t{1});
                }

                /**
                 * @brief Non-throwing multi-step run.
                 * @param steps Number of steps to run.
                 * @return The RunReport on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error>
                try_run(std::size_t steps) noexcept
                {
                    return drive([steps](std::size_t k) { return k < steps; });
                }

            private:
                friend class Runtime;

                explicit Graph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx))
                {
                }

                /** @brief Throws the carried error, or returns the report. */
                static Core::RunReport unwrap(Core::Result<Core::RunReport, Core::Error> r)
                {
                    if (!r)
                        throw Core::SushiException(std::move(r.error()));
                    return *r;
                }

                /**
                 * @brief Non-throwing run driver shared by every run/try_run entry.
                 *
                 * @param keep_going Called with the count of completed steps before
                 *                   each step; the loop continues while it is true.
                 * @return The aggregated RunReport, or a mapped Error on failure.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport, Core::Error> drive(KeepGoing keep_going) noexcept
                {
                    if (task_count_ == 0)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "Graph has no tasks to run."});
                    try
                    {
                        return drive_impl(keep_going);
                    }
                    catch (const Core::SushiException& e)
                    {
                        return Core::make_error(e.error());
                    }
                    catch (const sycl::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::kernel_failed, e.what()});
                    }
                    catch (const std::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed, e.what()});
                    }
                    catch (...)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed,
                                                            "unknown exception"});
                    }
                }

                template <class KeepGoing>
                Core::RunReport drive_impl(KeepGoing keep_going)
                {
                    using clock = std::chrono::steady_clock;
                    auto ms = [](clock::time_point a, clock::time_point b)
                    {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };

                    double compile_ms = 0.0;
                    if (dirty_)
                    {
                        const auto c0 = clock::now();
                        build_plan();
                        compile_ms = ms(c0, clock::now());
                    }

                    auto& engine = ctx_->get_execution_engine();
                    const auto before = engine.metrics();
                    const auto t0 = clock::now();

                    std::size_t executed = 0;
                    while (keep_going(executed))
                    {
                        reset_plan_state();
                        engine.execute(plan_); // lvalue -> non-consuming replay

                        // Advance every State after the step so its just-written
                        // next field becomes current — both for the following step
                        // and for the caller reading current() once the run ends.
                        for (auto& advance : step_advancers_)
                            advance();
                        ++executed;
                    }

                    const auto t1 = clock::now();
                    const auto after = engine.metrics();

                    const std::size_t total_tasks = task_count_ * executed;

                    Core::RunReport report;
                    report.is_successful = true;
                    report.total_tasks_executed = total_tasks;
                    report.graph_analysis_duration_ms = compile_ms;
                    report.total_duration_ms = ms(t0, t1);

                    // Attribute the tasks to the device class the default queue
                    // targets. Per-task placement and the reserved steal/byte
                    // counters are filled in as the engine exposes them.
                    Core::NumaNodeStatistics node;
                    try
                    {
                        auto& q = ctx_->get_queue(0);
                        if (q.get_device().is_gpu())
                            node.gpu_tasks_executed = total_tasks;
                        else
                            node.cpu_tasks_executed = total_tasks;
                    }
                    catch (...)
                    {
                        node.cpu_tasks_executed = total_tasks;
                        report.warnings.emplace_back("No SYCL queue available; task placement unknown.");
                    }

                    // Work-stealing breakdown over the whole run, from the
                    // scheduler's before/after counters.
                    const std::size_t stolen_intra = after.local_steals  - before.local_steals;
                    const std::size_t stolen_inter = after.remote_steals - before.remote_steals;
                    const std::size_t completed    = after.tasks_completed - before.tasks_completed;
                    node.tasks_stolen_intra_numa = stolen_intra;
                    node.tasks_stolen_inter_numa = stolen_inter;
                    const std::size_t stolen_total = stolen_intra + stolen_inter;
                    node.tasks_popped_local = (completed > stolen_total) ? (completed - stolen_total) : 0;
                    report.numa_statistics.push_back(node);

#ifdef SR_DISTRIBUTED
                    auto& d = report.distributed_statistics;
                    d.tasks_offloaded_to_network = ctx_->distributed_offloaded_count();
                    d.remote_nodes_involved =
                        static_cast<std::size_t>(ctx_->distributed_alive_workers());
#endif
                    return report;
                }

                // Builds a fresh backing graph from the recorded specs, compiles it,
                // caches the plan, and snapshots each node's compiled in-degree so a
                // replay can restore it. compile() drains its builder, so a fresh
                // BackingGraph is built whenever the topology changes.
                void build_plan()
                {
                    BackingGraph bg(*ctx_);
                    for (auto& spec : specs_)
                        spec(bg);
                    plan_ = bg.compile();

                    base_in_degree_.clear();
                    base_in_degree_.reserve(plan_.all_nodes.size());
                    for (auto& n : plan_.all_nodes)
                        base_in_degree_.push_back(n->in_degree.load(std::memory_order_relaxed));
                    sentinel_in_degree_ = plan_.sentinel
                        ? plan_.sentinel->in_degree.load(std::memory_order_relaxed) : 0;
                    dirty_ = false;
                }

                // Restores every node's dependency counter to its compiled value so
                // the cached plan can run again. The scheduler re-arms itself; this
                // resets the DAG-side state a previous run consumed.
                void reset_plan_state() noexcept
                {
                    const std::size_t count = plan_.all_nodes.size();
                    for (std::size_t i = 0; i < count && i < base_in_degree_.size(); ++i)
                        plan_.all_nodes[i]->in_degree.store(base_in_degree_[i],
                                                            std::memory_order_relaxed);
                    if (plan_.sentinel)
                        plan_.sentinel->in_degree.store(sentinel_in_degree_,
                                                        std::memory_order_relaxed);
#ifdef SR_DISTRIBUTED
                    for (auto& n : plan_.all_nodes)
                        n->offload_attempts_.store(0, std::memory_order_relaxed);
#endif
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                // Recorded task specifications: each replays one add() onto a fresh
                // backing graph. The fluent Graph is the source of truth so it can
                // rebuild and recompile when mutated.
                std::vector<std::function<void(BackingGraph&)>> specs_;

                // One per State update rule: advances the state and re-points its
                // view at the swapped buffers, run once per step boundary by run(n).
                std::vector<std::function<void()>> step_advancers_;

                std::size_t task_count_ = 0;

                // Cached compiled plan, valid while !dirty_; replayed across steps.
                Execution::CompiledGraph plan_;
                std::vector<std::int32_t> base_in_degree_;
                std::int32_t sentinel_in_degree_ = 0;
                bool dirty_ = true;
        };

        /**
         * @brief Entry point of the fluent API: owns hardware and hands out graphs.
         *
         * Create one with Runtime::create(), then call graph() to build work.
         * The runtime owns the RuntimeContext (hardware, queues, engine); graphs
         * borrow it, so the Runtime must outlive every graph it produces.
         */
        class Runtime
        {
            public:
                /**
                 * @brief Creates a runtime with default device selection.
                 * @param config Device-class filter passed to hardware discovery.
                 * @return A ready Runtime.
                 */
                static Runtime create(const Topology::HardwareFilterConfig& config = {})
                {
                    return Runtime(std::make_shared<Execution::RuntimeContext>(config));
                }

                /**
                 * @brief Starts a new, empty graph bound to this runtime.
                 * @return A fluent Graph; add tasks, then run() or try_run().
                 */
                Graph graph() { return Graph(ctx_); }

                /**
                 * @brief Allocates a typed shared-USM buffer owned by this runtime.
                 *
                 * The buffer borrows the runtime's context for allocation and
                 * frees itself on destruction, so it must not outlive the Runtime.
                 *
                 * @tparam T            Element type (trivially copyable for device use).
                 * @param count        Number of elements.
                 * @param device_index Target device queue; selects the owning context.
                 * @return An owning Buffer<T> handle.
                 */
                template <typename T>
                Buffer<T> buffer(std::size_t count, std::size_t device_index = 0)
                {
                    return Buffer<T>(ctx_, count, device_index);
                }

                /**
                 * @brief Allocates a double-buffered, time-evolving State field.
                 *
                 * Use this for data a stepped graph reads as "current" and writes
                 * as "next"; the runtime swaps the two each step. Like buffer(),
                 * it must not outlive the Runtime.
                 *
                 * @tparam T            Element type (trivially copyable for device use).
                 * @param count        Number of elements in each of the two buffers.
                 * @param device_index Target device queue for both buffers.
                 * @return An owning State<T> handle.
                 */
                template <typename T>
                State<T> state(std::size_t count, std::size_t device_index = 0)
                {
                    return State<T>(ctx_, count, device_index);
                }

                /** @brief Access to the underlying context for advanced use. */
                Execution::RuntimeContext& context() noexcept { return *ctx_; }

            private:
                explicit Runtime(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx))
                {
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;
        };

    } // namespace API

    // Promote the entry points into the SushiRuntime namespace so users write
    // `Runtime::create()` after `using namespace SushiRuntime;`. The graph type
    // stays API::Graph (reached through auto) because the name Graph already
    // denotes a namespace at this scope.
    using API::Runtime;
    using Core::Error;
    using Core::Errc;
    using Core::RunReport;
    using Core::SushiException;

} // namespace SushiRuntime
