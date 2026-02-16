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
#include <functional>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

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
                 * @brief Adds a new node with manual dependencies.
                 * @param work The SYCL kernel or transfer operation.
                 * @param dependencies Events that must finish before this task starts.
                 */
                void add_node(TaskWork work, const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief Adds a node with automatic dependency tracking.
                 * 
                 * This method tracks memory addresses to build the dependency graph.
                 * 
                 * @param work The work to execute.
                 * @param read_access Memory addresses that will be read.
                 * @param write_access Memory addresses that will be written to.
                 * @param dependencies Extra manual dependencies.
                 */
                void add_node(TaskWork work, 
                             const std::vector<void*>& read_access, 
                             const std::vector<void*>& write_access,
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
