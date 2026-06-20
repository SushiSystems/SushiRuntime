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
                    auto kernel = [fn = std::forward<F>(fn), n](sycl::handler& h)
                    {
                        h.parallel_for(sycl::range<1>(n),
                                       [fn](sycl::id<1> idx) { fn(idx[0]); });
                    };
                    tg_->add_node(std::move(kernel));
                    ++task_count_;
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
                    auto kernel = [fn = std::forward<F>(fn), n](sycl::handler& h)
                    {
                        fn(std::size_t{0}, n, h);
                    };
                    tg_->add_node(std::move(kernel));
                    ++task_count_;
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
                    tg_->add_node(std::forward<F>(fn));
                    ++task_count_;
                    return *this;
                }

                /**
                 * @brief Compiles and runs the graph, blocking until it completes.
                 *
                 * Throws on failure. Use try_run() for the non-throwing channel.
                 *
                 * @return A RunReport summarising the run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run()
                {
                    auto result = try_run();
                    if (!result)
                        throw Core::SushiException(std::move(result.error()));
                    return *result;
                }

                /**
                 * @brief Compiles and runs the graph without throwing.
                 *
                 * Synchronous failures (empty graph, compilation) and device
                 * faults surfacing from the queue's async handler are both mapped
                 * into the Error channel.
                 *
                 * @return The RunReport on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error> try_run() noexcept
                {
                    if (task_count_ == 0)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "Graph has no tasks to run."});
                    try
                    {
                        return run_impl();
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
                        return Core::make_error(Core::Error{Core::Errc::compile_failed, "unknown exception"});
                    }
                }

                /** @brief Number of tasks added so far. */
                std::size_t size() const noexcept { return task_count_; }

            private:
                friend class Runtime;

                explicit Graph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)), tg_(std::make_unique<BackingGraph>(*ctx_))
                {
                }

                Core::RunReport run_impl()
                {
                    using clock = std::chrono::steady_clock;
                    auto& engine = ctx_->get_execution_engine();
                    const auto before = engine.metrics();

                    const auto t0 = clock::now();
                    auto compiled = tg_->compile();
                    const auto t1 = clock::now();
                    engine.execute(std::move(compiled));
                    const auto t2 = clock::now();

                    const auto after = engine.metrics();

                    auto ms = [](clock::time_point a, clock::time_point b)
                    {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };

                    Core::RunReport report;
                    report.is_successful = true;
                    report.total_tasks_executed = task_count_;
                    report.graph_analysis_duration_ms = ms(t0, t1);
                    report.total_duration_ms = ms(t0, t2);

                    // Attribute the tasks to the device class the default queue
                    // targets, on its NUMA node. Per-task placement and the
                    // reserved steal/byte counters are filled in as the engine
                    // exposes them.
                    Core::NumaNodeStatistics node;
                    try
                    {
                        auto& q = ctx_->get_queue(0);
                        if (q.get_device().is_gpu())
                            node.gpu_tasks_executed = task_count_;
                        else
                            node.cpu_tasks_executed = task_count_;
                    }
                    catch (...)
                    {
                        node.cpu_tasks_executed = task_count_;
                        report.warnings.emplace_back("No SYCL queue available; task placement unknown.");
                    }

                    // Work-stealing breakdown for this run, from the scheduler's
                    // before/after counters. Intra/inter-NUMA map to node-local vs
                    // cross-node steals; popped-local is what is left after steals.
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

                std::shared_ptr<Execution::RuntimeContext> ctx_;
                std::unique_ptr<BackingGraph> tg_;
                std::size_t task_count_ = 0;
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
                 * @tparam T      Element type (trivially copyable for device use).
                 * @param count   Number of elements.
                 * @param numa_id Target NUMA node; selects the owning queue.
                 * @return An owning Buffer<T> handle.
                 */
                template <typename T>
                Buffer<T> buffer(std::size_t count, int numa_id = 0)
                {
                    return Buffer<T>(ctx_, count, numa_id);
                }

                /**
                 * @brief Allocates a double-buffered, time-evolving State field.
                 *
                 * Use this for data a stepped graph reads as "current" and writes
                 * as "next"; the runtime swaps the two each step. Like buffer(),
                 * it must not outlive the Runtime.
                 *
                 * @tparam T      Element type (trivially copyable for device use).
                 * @param count   Number of elements in each of the two buffers.
                 * @param numa_id Target NUMA node for both buffers.
                 * @return An owning State<T> handle.
                 */
                template <typename T>
                State<T> state(std::size_t count, int numa_id = 0)
                {
                    return State<T>(ctx_, count, numa_id);
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
