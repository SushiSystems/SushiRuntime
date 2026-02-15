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
#include <sycl/sycl.hpp>
#include <functional>
#include <vector>
#include <memory>
#include "../core/sushi_ptr.hpp"

namespace SushiRuntime 
{
    namespace Execution 
    { 
        class RuntimeContext; 
    }

    namespace Graph
    {
        /**
         * @brief task work type defined as a sycl kernel or transfer.
         */
        using TaskWork = std::function<void(sycl::handler&)>;

        /**
         * @brief manages and optimizes dependencies between async tasks.
         * 
         * TaskGraph collects work (kernels), analyzes data dependencies,
         * and executes them in the best order on the hardware. 
         * it can also perform optimizations like kernel fusion.
         */
        class TaskGraph 
        {
            public:
                /**
                 * @brief creates a new task graph.
                 * @param ctx reference to the associated RuntimeContext.
                 */
                TaskGraph(SushiRuntime::Execution::RuntimeContext& ctx);
                
                /** @brief destroys the graph and cleans up task states. */
                ~TaskGraph();

                /**
                 * @brief adds a new node with manual dependencies.
                 * @param work the sycl kernel or transfer operation.
                 * @param dependencies events that must finish before this task starts.
                 */
                void add_node(TaskWork work, const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief adds a node with automatic dependency tracking.
                 * 
                 * this method tracks memory addresses to build the dependency graph.
                 * 
                 * @param work the work to execute.
                 * @param read_access memory addresses that will be read.
                 * @param write_access memory addresses that will be written to.
                 * @param dependencies extra manual dependencies.
                 */
                void add_node(TaskWork work, 
                             const std::vector<void*>& read_access, 
                             const std::vector<void*>& write_access,
                             const std::vector<sycl::event>& dependencies = {});

                /**
                 * @brief analyzes, optimizes, and submits the graph to hardware queues.
                 * @return sycl::event event indicating the completion of the graph.
                 */
                sycl::event execute();

            private:
                struct Impl;
                /** @brief pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_;
                /** @brief reference to the runtime context. */
                SushiRuntime::Execution::RuntimeContext& ctx_;
        };
    
    } // namespace Graph
} // namespace SushiRuntime
