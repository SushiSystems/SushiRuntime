/**************************************************************************/
/* task_graph.hpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once
#include <memory>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/graph/task_types.hpp>

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
                 * @brief Adds a semantic task into the graph with full profiler and optimization metadata.
                 * 
                 * @param meta Definitions containing the operation type and properties.
                 * @param read_access Memory pointers that this operation reads.
                 * @param write_access Memory pointers that this operation overwrites or updates.
                 * @param fallback_work A lambda function executed safely if the Operation is not fused by the optimizer.
                 * @param dependencies Additional explicit user dependencies.
                 */
                void add_task(const TaskMetadata& meta,
                              const std::vector<void*>& read_access,
                              const std::vector<void*>& write_access,
                              HostWork fallback_work,
                              const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief Analyzes, optimizes, and submits the graph to hardware queues.
                 * @return sycl::event Event indicating the completion of the graph.
                 */
                sycl::event execute();

            private:
                struct Impl;
                /** @brief Pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_;
                /** @brief Reference to the runtime context. */
                SushiRuntime::Execution::RuntimeContext& ctx_;
        };
    
    } // namespace Graph
} // namespace SushiRuntime
