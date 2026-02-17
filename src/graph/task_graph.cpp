/**************************************************************************/
/* task_graph.cpp                                                         */
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

#include <future>
#include <thread>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/dependency_tracker.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime 
{
    namespace Graph
    {
        using namespace SushiRuntime::Execution;
        using namespace SushiRuntime::Core;

        /**
         * @brief Private implementation of the TaskGraph.
         */
        struct TaskGraph::Impl : public Core::RefCounted
        {
            /** @brief Storage for all nodes in the graph. */
            std::vector<sushi_ptr<Node>> nodes;
            /** @brief Tracker to handle automatic dependencies. */
            DependencyTracker tracker;
            /** @brief Reference to the runtime context. */
            RuntimeContext& context;

            Impl(RuntimeContext& ctx) : context(ctx) {}
            ~Impl() = default;
        };

        TaskGraph::TaskGraph(RuntimeContext& ctx) : impl_(make_sushi<Impl>(ctx)), ctx_(ctx) 
        {
            SR_LOG_DEBUG("Production analyzer initialized.");
        }

        TaskGraph::~TaskGraph() = default;

        /**
         * @brief Adds a task node with manual SYCL event dependencies.
         */
        void TaskGraph::add_node(TaskWork work, const std::vector<sycl::event>& dependencies) 
        {
            auto node = make_sushi<Node>();
            node->work = std::move(work);
            node->sycl_deps = dependencies;
            
            SR_LOG_DEBUG("Node added with {} manual dependencies.", dependencies.size());
            impl_->nodes.push_back(std::move(node));
        }

        /**
         * @brief Adds a task node and resolves USM data dependencies automatically.
         */
        void TaskGraph::add_node(TaskWork work, 
                                 const std::vector<void*>& read_access, 
                                 const std::vector<void*>& write_access,
                                 const std::vector<sycl::event>& dependencies)
        {
            auto node = make_sushi<Node>();
            node->work = std::move(work);
            node->sycl_deps = dependencies;
            
            // resolve usm dependencies for reads and writes
            for(void* ptr : read_access) impl_->tracker.resolve(node.get(), ptr, false);
            for(void* ptr : write_access) impl_->tracker.resolve(node.get(), ptr, true);

            SR_LOG_DEBUG("Node added with automatic dependency tracking (R: {}, W: {}).", read_access.size(), write_access.size());
            
            impl_->nodes.push_back(std::move(node));
        }

        /**
         * @brief Executes the entire graph using the task scheduler.
         */
        // TODO: refactor this function to be non-blocking and return a future immediately.
        sycl::event TaskGraph::execute() 
        {
            SR_LOG_INFO("Executing DAG with {} nodes.", impl_->nodes.size());

            if(impl_->nodes.empty()) return sycl::event{};

            /** @brief Setup local scheduler for this execution. */
            SushiRuntime::Scheduler::TaskScheduler scheduler(ctx_);

            /** @brief Identify roots and create a sentinel node for completion. */
            std::vector<Node*> roots;
            auto sentinel = make_sushi<Node>();
            
            auto* sched_ptr = &scheduler;
            std::promise<void> promise;
            auto future = promise.get_future();
            
            sentinel->work = [sched_ptr, &promise](sycl::handler& h) 
            {
                h.host_task([sched_ptr, &promise](){
                   sched_ptr->running.store(false, std::memory_order_release);
                   promise.set_value();
                });
            };

            for (auto& node : impl_->nodes) 
            {
                if (!node) continue;
                // find graph roots (nodes with no incoming dependencies)
                if (node->in_degree.load(std::memory_order_relaxed) == 0) 
                {
                    roots.push_back(node.get());
                }
                
                // link graph leaves to the sentinel node
                if (node->successor_list.load(std::memory_order_relaxed) == nullptr) 
                {
                    impl_->tracker.add_dependency(node.get(), sentinel.get());
                }
            }

            /** @brief Submit all root nodes to start execution. */
            for(auto* root : roots) 
            {
                scheduler.submit(root);
            }

            /** @brief Join execution loop on the main thread as worker 0. */
            scheduler.dispatch_loop(0);
            
            // Wait for the sentinel to signal completion
            future.wait();

            SR_LOG_INFO("DAG execution completed.");
            return sycl::event{};
        }

    } // namespace Graph
} // namespace SushiRuntime