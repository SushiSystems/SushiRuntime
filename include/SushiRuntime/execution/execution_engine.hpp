/**************************************************************************/
/* execution_engine.hpp                                                   */
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

#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

namespace SushiRuntime
{
    namespace Graph
    {
        struct Node;
    }

#ifdef SUSHI_DISTRIBUTED
    namespace Scheduler { class TaskScheduler; }
#endif

    namespace Execution
    {
        class RuntimeContext;

        /**
         * @brief Compiled representation of a task graph ready for execution.
         *
         * TaskGraph::compile() produces this struct. It contains the topologically
         * resolved root nodes and the sentinel node that signals graph completion.
         * The ExecutionEngine consumes it; TaskGraph only produces it.
         */
struct CompiledGraph
        {
            /** @brief Nodes with no pending incoming dependencies (DAG entry points). */
            std::vector<SushiRuntime::Graph::Node*> roots;

            /** @brief Sentinel node injected at the end of the graph. */
            sushi_ptr<SushiRuntime::Graph::Node> sentinel;

            /** * @brief Owns all nodes to keep them alive during execution. 
             * This prevents Use-After-Free bugs since internal dependencies rely on raw pointers.
             */
            std::vector<sushi_ptr<SushiRuntime::Graph::Node>> all_nodes;
        };

        /**
         * @brief Abstract interface for graph execution strategies.
         *
         * Separates the "how to run a graph" decision from both TaskGraph (which
         * is only responsible for building the DAG) and RuntimeContext (which is
         * only responsible for hardware resources). New execution strategies
         * (e.g., dry-run, profiling, replay) can be plugged in without touching
         * either of those classes. [OCP compliant]
         */
        class SUSHIRUNTIME_API IExecutionEngine
        {
        public:
            virtual ~IExecutionEngine() = default;

            /**
             * @brief Submits a compiled graph for execution and blocks until completion.
             *
             * @param graph The pre-compiled graph produced by TaskGraph::compile().
             * @return sycl::event Event signalling that the entire graph has finished.
             */
            virtual sycl::event execute(CompiledGraph graph) = 0;

            /**
             * @brief Signals the engine to stop accepting new work and drain active tasks.
             *
             * Safe to call from any thread. Blocks until all inflight work is complete.
             */
            virtual void shutdown() = 0;
        };

        /**
         * @brief Default work-stealing execution engine backed by TaskScheduler.
         *
         * Owns a single persistent TaskScheduler that is started once on construction
         * and stopped on shutdown(). Each call to execute() rearms the scheduler for
         * a new graph run without recreating worker threads. [Madde 7 fix]
         *
         * Lifecycle is managed by RuntimeContext::Impl so that it is guaranteed to be
         * shut down before any hardware queues or allocators are destroyed. [Madde 18 fix]
         */
        class SUSHIRUNTIME_API ExecutionEngine final : public IExecutionEngine
        {
        public:
            /**
             * @brief Constructs the engine and starts the persistent worker pool.
             * @param ctx The owning RuntimeContext. Must outlive this engine.
             */
            explicit ExecutionEngine(RuntimeContext& ctx);

            /**
             * @brief Shuts down the worker pool and waits for all threads to join.
             *
             * Called by RuntimeContext::Impl::~Impl() before any other resource
             * is destroyed, ensuring no dangling context references exist.
             */
            ~ExecutionEngine() override;

            // Non-copyable / non-movable – owns live threads.
            ExecutionEngine(const ExecutionEngine&) = delete;
            ExecutionEngine& operator=(const ExecutionEngine&) = delete;

            /**
             * @brief Executes a compiled graph synchronously.
             *
             * Injects all root nodes into the persistent scheduler, re-arms the
             * running flag for this execution round, then blocks on the sentinel's
             * completion promise. Does NOT recreate any threads.
             *
             * @param graph Compiled graph to execute.
             * @return sycl::event The hardware event from the sentinel node.
             */
            sycl::event execute(CompiledGraph graph) override;

            /**
             * @brief Drains inflight work and stops the worker pool.
             *
             * Idempotent — safe to call multiple times.
             */
            void shutdown() override;

#ifdef SUSHI_DISTRIBUTED
            /** @brief Access to the persistent scheduler, so Master mode can attach the coordinator. */
            SushiRuntime::Scheduler::TaskScheduler& scheduler_ref();
#endif

        private:
            struct Impl;
            sushi_ptr<Impl> impl_;
        };

    } // namespace Execution
} // namespace SushiRuntime
