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
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
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
    namespace Api
    {
        // The lower-level graph builder this layer wraps. Spelled out fully to
        // avoid clashing with the Api::Graph class name below.
        using BackingGraph = ::SushiRuntime::Graph::TaskGraph;

        /**
         * @brief A function callable once per element index: void(size_t).
         *
         * Submitted as a SYCL parallel_for over [0, n); the runtime picks the
         * device and the work-group mapping.
         */
        template <class F>
        concept PerElement = std::invocable<F, std::size_t> &&
                             !std::invocable<F, std::size_t, std::size_t, sycl::handler&>;

        /**
         * @brief A raw kernel that drives a sycl::handler directly: void(sycl::handler&).
         */
        template <class F>
        concept RawKernel = std::invocable<F, sycl::handler&>;

        /**
         * @brief A range kernel that receives its bounds and a handler:
         *        void(size_t begin, size_t end, sycl::handler&).
         */
        template <class F>
        concept RangeKernel = std::invocable<F, std::size_t, std::size_t, sycl::handler&>;

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
                template <PerElement F>
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
                template <RangeKernel F>
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
                template <RawKernel F>
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
                [[nodiscard]] std::expected<Core::RunReport, Core::Error> try_run() noexcept
                {
                    if (task_count_ == 0)
                        return std::unexpected(Core::Error{Core::Errc::invalid_graph,
                                                           "Graph has no tasks to run."});
                    try
                    {
                        return run_impl();
                    }
                    catch (const Core::SushiException& e)
                    {
                        return std::unexpected(e.error());
                    }
                    catch (const sycl::exception& e)
                    {
                        return std::unexpected(Core::Error{Core::Errc::kernel_failed, e.what()});
                    }
                    catch (const std::exception& e)
                    {
                        return std::unexpected(Core::Error{Core::Errc::compile_failed, e.what()});
                    }
                }

                /** @brief Number of tasks added so far. */
                std::size_t size() const noexcept { return task_count_; }

            private:
                friend class Runtime;

                explicit Graph(Execution::RuntimeContext& ctx)
                    : ctx_(&ctx), tg_(std::make_unique<BackingGraph>(ctx))
                {
                }

                Core::RunReport run_impl()
                {
                    using clock = std::chrono::steady_clock;
                    const auto t0 = clock::now();
                    auto compiled = tg_->compile();
                    const auto t1 = clock::now();
                    ctx_->get_execution_engine().execute(std::move(compiled));
                    const auto t2 = clock::now();

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
                    Core::NumaNodeStats node;
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
                    report.numa_stats.push_back(node);

#ifdef SR_DISTRIBUTED
                    auto& d = report.distributed_stats;
                    d.tasks_offloaded_to_network = ctx_->distributed_offloaded_count();
                    d.remote_nodes_involved =
                        static_cast<std::size_t>(ctx_->distributed_alive_workers());
#endif
                    return report;
                }

                Execution::RuntimeContext* ctx_;
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
                Graph graph() { return Graph(*ctx_); }

                /** @brief Access to the underlying context for advanced use. */
                Execution::RuntimeContext& context() noexcept { return *ctx_; }

            private:
                explicit Runtime(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx))
                {
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;
        };

    } // namespace Api

    // Promote the entry points into the SushiRuntime namespace so users write
    // `Runtime::create()` after `using namespace SushiRuntime;`. The graph type
    // stays Api::Graph (reached through auto) because the name Graph already
    // denotes a namespace at this scope.
    using Api::Runtime;
    using Core::Error;
    using Core::Errc;
    using Core::RunReport;
    using Core::SushiException;

} // namespace SushiRuntime
