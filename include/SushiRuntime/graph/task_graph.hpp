/**************************************************************************/
/* task_graph.hpp                                                         */
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

#include <functional>
#include <memory>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime 
{
    namespace Execution 
    { 
        class RuntimeContext; 
    }

    namespace Graph
    {
        /**
         * @brief Task work type defined as a SYCL kernel or transfer.
         */
        using TaskWork = std::function<void(sycl::handler&)>;

        /**
         * @brief Task work type for host-side (library) execution. 
         */
        using HostWork = std::function<sycl::event(sycl::queue&, const std::vector<sycl::event>&)>;

        /**
         * @brief Manages and optimizes dependencies between async tasks.
         * 
         * TaskGraph collects work (kernels), analyzes data dependencies,
         * and executes them in the best order on the hardware. 
         * It can also perform optimizations like kernel fusion.
         */
        class SUSHIRUNTIME_API TaskGraph 
        {
            public:
                /**
                 * @brief Creates a new task graph.
                 * @param ctx Reference to the associated RuntimeContext.
                 */
                TaskGraph(SushiRuntime::Execution::RuntimeContext& ctx);
                
                /** @brief Destroys the graph and cleans up task states. */
                ~TaskGraph();

                /** @brief Disable copy for Pimpl safety and state integrity. */
                TaskGraph(const TaskGraph&) = delete;
                TaskGraph& operator=(const TaskGraph&) = delete;

                /**
                 * @brief Adds a task for the device (GPU/CPU).
                 * 
                 * Use this for your own SYCL kernels or memory transfers.
                 * It gives you a sycl::handler to define the work.
                 * 
                 * @param work Function that uses a sycl::handler to run work.
                 * @param dependencies Manual events to wait for before starting.
                 */
                void add_node(TaskWork work, const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief Adds a device task with automatic dependency tracking.
                 * 
                 * The runtime checks the memory addresses (USM) to automatically 
                 * find when this task can safely run.
                 * 
                 * @param work Function that uses a sycl::handler to run work.
                 * @param read_access List of memory addresses this task reads from.
                 * @param write_access List of memory addresses this task writes to.
                 * @param dependencies Additional manual events to wait for.
                 */
                void add_node(TaskWork work, 
                             const std::vector<void*>& read_access, 
                             const std::vector<void*>& write_access,
                             const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief Adds a semantic task into the graph with explicit device and host fallback work.
                 * 
                 * Use this overload for kernel-capable tasks that need a real device lambda.
                 * The host fallback is still tracked for CPU-only or library execution paths.
                 * 
                 * @param meta Definitions containing the operation type and properties.
                 * @param read_access Memory pointers that this operation reads.
                 * @param write_access Memory pointers that this operation overwrites or updates.
                 * @param device_work The actual SYCL handler body used in kernel mode.
                 * @param fallback_work A lambda executed on the host queue when host execution is selected.
                 * @param dependencies Additional explicit user dependencies.
                 */
                void add_task(const TaskMetadata& meta,
                              const std::vector<void*>& read_access,
                              const std::vector<void*>& write_access,
                              TaskWork device_work,
                              HostWork fallback_work,
                              const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief Adds a semantic task into the graph with host-side fallback only.
                 * 
                 * This is the compatibility overload for host-executed semantic tasks.
                 * Kernel-capable tasks should prefer the overload that also receives device_work.
                 * 
                 * @param meta Definitions containing the operation type and properties.
                 * @param read_access Memory pointers that this operation reads.
                 * @param write_access Memory pointers that this operation overwrites or updates.
                 * @param fallback_work A lambda function executed safely on host execution.
                 * @param dependencies Additional explicit user dependencies.
                 */
                void add_task(const TaskMetadata& meta,
                              const std::vector<void*>& read_access,
                              const std::vector<void*>& write_access,
                              HostWork fallback_work,
                              const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief Resolves data dependencies and produces a CompiledGraph ready to execute.
                 *
                 * Links leaf nodes to a sentinel completion node, identifies root nodes
                 * (zero in-degree), and transfers node ownership to the returned
                 * CompiledGraph. Does not submit any work to hardware.
                 *
                 * @warning Not thread-safe with respect to add_node() / add_task().
                 *          All nodes must be inserted before compile() is called.
                 *
                 * @return CompiledGraph Compiled plan; call execute() on it to run.
                 */
                SushiRuntime::Execution::CompiledGraph compile();

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };
    
    } // namespace Graph
} // namespace SushiRuntime
