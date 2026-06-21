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
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/buffer.hpp>
#include <SushiRuntime/api/detail/run_report_builder.hpp>
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
                Graph(Graph&&) = default;
                Graph& operator=(Graph&&) = default;
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
                    auto view = view_for(s);
                    State<T>* sp = &s;

                    specs_.push_back([fn = std::forward<F>(fn), view, sp](BackingGraph& bg)
                    {
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

                    register_advance(s, view);
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a coupled update rule reading one other field.
                 *
                 * Writes @p out's next field from @p out's current field and the
                 * current field of @p in0 — the building block for coupled systems
                 * (e.g. reaction-diffusion). All reads see the previous step's data
                 * (a Jacobi update): every State advances only after all of a step's
                 * kernels have run, so writing one field never changes what another
                 * reads in the same step. Each field must have exactly one update
                 * rule per step.
                 *
                 * @tparam T   Element type of the written field.
                 * @tparam A   Element type of the read field.
                 * @param out  The field this rule writes; must outlive the graph.
                 * @param in0  A field this rule reads; must outlive the graph.
                 * @param fn   Invoked as fn(i, const T* cur, T* next, const A* a).
                 * @return *this, for chaining.
                 */
                template <typename T, typename A, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                              const T*, T*, const A*>, int> = 0>
                Graph& add(State<T>& out, State<A>& in0, F&& fn)
                {
                    auto ov = view_for(out);
                    auto av = view_for(in0);
                    State<T>* op = &out;
                    State<A>* a0 = &in0;

                    specs_.push_back([fn = std::forward<F>(fn), ov, av, op, a0](BackingGraph& bg)
                    {
                        const std::size_t n = op->size();
                        bg.add_node([fn, ov, av, n](sycl::handler& h)
                        {
                            const T* cur = ov->cur;
                            T* next = ov->next;
                            const A* a = av->cur;
                            h.parallel_for(sycl::range<1>(n),
                                           [=](sycl::id<1> i) { fn(i[0], cur, next, a); });
                        }, { op->read_key(), a0->read_key() }, { op->write_key() });
                    });

                    register_advance(out, ov);
                    ++task_count_;
                    dirty_ = true;
                    return *this;
                }

                /**
                 * @brief Adds a coupled update rule reading two other fields.
                 *
                 * As the single-input coupling overload, with two read fields.
                 *
                 * @tparam T   Element type of the written field.
                 * @tparam A   Element type of the first read field.
                 * @tparam B   Element type of the second read field.
                 * @param out  The field this rule writes; must outlive the graph.
                 * @param in0  First read field; must outlive the graph.
                 * @param in1  Second read field; must outlive the graph.
                 * @param fn   Invoked as fn(i, const T* cur, T* next, const A* a,
                 *             const B* b).
                 * @return *this, for chaining.
                 */
                template <typename T, typename A, typename B, class F,
                          std::enable_if_t<std::is_invocable_v<F, std::size_t,
                              const T*, T*, const A*, const B*>, int> = 0>
                Graph& add(State<T>& out, State<A>& in0, State<B>& in1, F&& fn)
                {
                    auto ov = view_for(out);
                    auto av = view_for(in0);
                    auto bv = view_for(in1);
                    State<T>* op = &out;
                    State<A>* a0 = &in0;
                    State<B>* b1 = &in1;

                    specs_.push_back([fn = std::forward<F>(fn), ov, av, bv, op, a0, b1](BackingGraph& bg)
                    {
                        const std::size_t n = op->size();
                        bg.add_node([fn, ov, av, bv, n](sycl::handler& h)
                        {
                            const T* cur = ov->cur;
                            T* next = ov->next;
                            const A* a = av->cur;
                            const B* b = bv->cur;
                            h.parallel_for(sycl::range<1>(n),
                                           [=](sycl::id<1> i) { fn(i[0], cur, next, a, b); });
                        }, { op->read_key(), a0->read_key(), b1->read_key() },
                           { op->write_key() });
                    });

                    register_advance(out, ov);
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

                    // Point every State view at its current/next buffers before the
                    // first step. Covers both a fresh build and a replay continuing
                    // from a previous run.
                    for (auto& resync : view_resyncs_)
                        resync();

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
                    return detail::build_run_report(*ctx_, before, after, total_tasks,
                                                    compile_ms, ms(t0, t1));
                }

                // Returns the shared view for a State, creating it (and registering
                // its resync) on first use. Every kernel touching a State captures
                // the same view, so one advancer keeps all references current — the
                // basis for correct coupled (multi-field) updates.
                template <typename T>
                std::shared_ptr<detail::StateView<T>> view_for(State<T>& s)
                {
                    const void* key = &s;
                    auto it = state_views_.find(key);
                    if (it != state_views_.end())
                        return std::static_pointer_cast<detail::StateView<T>>(it->second);

                    auto view = std::make_shared<detail::StateView<T>>();
                    state_views_.emplace(key, view);
                    State<T>* sp = &s;
                    view_resyncs_.push_back([sp, view]()
                    {
                        view->cur = sp->current();
                        view->next = sp->next();
                    });
                    return view;
                }

                // Registers the once-per-step advance for a written State. Deduped so
                // a State advances exactly once per step (one update rule per field).
                template <typename T>
                void register_advance(State<T>& s, std::shared_ptr<detail::StateView<T>> view)
                {
                    const void* key = &s;
                    if (!advanced_states_.insert(key).second)
                        return;
                    State<T>* sp = &s;
                    step_advancers_.push_back([sp, view]()
                    {
                        sp->advance();
                        view->cur = sp->current();
                        view->next = sp->next();
                    });
                }

                // Builds a fresh backing graph from the recorded specs, compiles it,
                // and caches the plan. The compiled plan snapshots its own replay
                // baseline (in-degrees) so the fluent layer never touches Node's
                // scheduler-internal state. compile() drains its builder, so a fresh
                // BackingGraph is built whenever the topology changes.
                void build_plan()
                {
                    BackingGraph bg(*ctx_);
                    for (auto& spec : specs_)
                        spec(bg);
                    plan_ = bg.compile();
                    plan_.capture_base_state();
                    dirty_ = false;
                }

                // Restores the cached plan's per-run state so it can run again. The
                // scheduler re-arms itself; this resets the DAG-side state a previous
                // run consumed, owned by CompiledGraph next to the Node definition.
                void reset_plan_state() noexcept
                {
                    plan_.rearm();
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                // Recorded task specifications: each replays one add() onto a fresh
                // backing graph. The fluent Graph is the source of truth so it can
                // rebuild and recompile when mutated.
                std::vector<std::function<void(BackingGraph&)>> specs_;

                // One per State update rule: advances the state and re-points its
                // view at the swapped buffers, run once per step boundary by run(n).
                std::vector<std::function<void()>> step_advancers_;

                // One shared StateView per State, captured by every kernel that
                // touches it. Type-erased; view_for() casts back to the typed view.
                std::unordered_map<const void*, std::shared_ptr<void>> state_views_;
                // States that already have an advancer (one writer per State).
                std::unordered_set<const void*> advanced_states_;
                // Points each view at its state's current/next; run before each run.
                std::vector<std::function<void()>> view_resyncs_;

                std::size_t task_count_ = 0;

                // Cached compiled plan, valid while !dirty_; replayed across steps.
                // It owns its own replay baseline (see capture_base_state/rearm).
                Execution::CompiledGraph plan_;
                bool dirty_ = true;
        };

#ifdef SR_DISTRIBUTED
        /**
         * @brief Fluent handle to a Master-mode distributed cluster.
         *
         * Returned by Runtime::cluster(), which promotes the runtime to Master and
         * wires the transport. This handle bundles the cluster's tuning knobs and
         * telemetry so callers do not reach into RuntimeContext's distributed_*
         * surface directly. Offloadable tasks are still added with op_id-bearing
         * task metadata; this handle governs the cluster, not the graph.
         */
        class Cluster
        {
            public:
                /**
                 * @brief Blocks until @p n workers have registered, or the timeout.
                 * @return true if the cluster reached @p n live workers in time.
                 */
                bool wait_for_workers(int n, std::chrono::milliseconds timeout)
                {
                    const auto deadline = std::chrono::steady_clock::now() + timeout;
                    while (ctx_->distributed_alive_workers() < n)
                    {
                        if (std::chrono::steady_clock::now() >= deadline)
                            return false;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    return true;
                }

                /** @brief Live (registered) worker count. */
                int alive_workers() const { return ctx_->distributed_alive_workers(); }

                /** @brief Sets the worker-silence timeout (see RuntimeContext). */
                Cluster& liveness_timeout(std::chrono::milliseconds t)
                {
                    ctx_->distributed_set_liveness_timeout(t);
                    return *this;
                }

                /** @brief Sets the per-task offload deadline (see RuntimeContext). */
                Cluster& task_deadline(std::chrono::milliseconds t)
                {
                    ctx_->distributed_set_task_deadline(t);
                    return *this;
                }

                /** @brief Tasks shipped to workers. */
                std::uint64_t offloaded() const { return ctx_->distributed_offloaded_count(); }
                /** @brief Offloadable tasks the policy kept local. */
                std::uint64_t declined() const { return ctx_->distributed_declined_count(); }
                /** @brief Tasks re-injected after a worker death / deadline / failure. */
                std::uint64_t redispatched() const { return ctx_->distributed_redispatched_count(); }
                /** @brief Writebacks rejected by the version fence. */
                std::uint64_t stale_writebacks() const { return ctx_->distributed_stale_writeback_count(); }

            private:
                friend class Runtime;
                explicit Cluster(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)) {}
                std::shared_ptr<Execution::RuntimeContext> ctx_;
        };
#endif // SR_DISTRIBUTED

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

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Promotes this runtime to a distributed Master and returns
                 *        a Cluster handle governing it.
                 *
                 * Thin fluent wrapper over RuntimeContext::enable_distributed_master:
                 * wires the transport and starts serving. Until called, the runtime
                 * is a fully offline single machine (zero distributed overhead).
                 *
                 * @param transport Connected master-side transport (one peer per worker).
                 * @param kernels   Registry shared with this binary's kernels.
                 * @param policy    Offload policy (null → LeastLoadedOffloadPolicy).
                 * @return A Cluster handle for tuning and telemetry.
                 */
                Cluster cluster(std::shared_ptr<Distributed::ITransport> transport,
                                sushi_ptr<Distributed::KernelRegistry> kernels,
                                std::shared_ptr<Distributed::IOffloadPolicy> policy = nullptr)
                {
                    ctx_->enable_distributed_master(std::move(transport),
                                                    std::move(kernels), std::move(policy));
                    return Cluster(ctx_);
                }
#endif // SR_DISTRIBUTED

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
