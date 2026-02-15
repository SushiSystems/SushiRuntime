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

#include "../../include/SushiRuntime/graph/task_graph.hpp"
#include "../../include/SushiRuntime/execution/runtime_context.hpp"
#include "../../include/SushiRuntime/core/logger.hpp"
#include "../../include/SushiRuntime/graph/node.hpp"
#include "../../include/SushiRuntime/graph/dependency_tracker.hpp"
#include "../../include/SushiRuntime/scheduler/task_scheduler.hpp"
#include <sycl/sycl.hpp>
#include <vector>
#include <thread>
#include <future>

namespace SushiRuntime 
{
    namespace Graph
    {
        using namespace SushiRuntime::Execution;
        using namespace SushiRuntime::Core;

        /**
         * @brief private implementation of the TaskGraph.
         */
        struct TaskGraph::Impl : public Core::RefCounted
        {
            /** @brief storage for all nodes in the graph. */
            std::vector<sushi_ptr<Node>> nodes;
            /** @brief tracker to handle automatic dependencies. */
            DependencyTracker tracker;
            /** @brief reference to the runtime context. */
            RuntimeContext& context;

            Impl(RuntimeContext& ctx) : context(ctx) {}
            ~Impl() = default;
        };

        TaskGraph::TaskGraph(RuntimeContext& ctx) : impl_(make_sushi<Impl>(ctx)), ctx_(ctx) 
        {
            SR_LOG_DEBUG("production analyzer initialized.");
        }

        TaskGraph::~TaskGraph() = default;

        /**
         * @brief adds a task node with manual sycl event dependencies.
         */
        void TaskGraph::add_node(TaskWork work, const std::vector<sycl::event>& dependencies) 
        {
            auto node = make_sushi<Node>();
            node->work = std::move(work);
            node->sycl_deps = dependencies;
            
            SR_LOG_DEBUG("node added with {} manual dependencies.", dependencies.size());
            impl_->nodes.push_back(std::move(node));
        }

        /**
         * @brief adds a task node and resolves usm data dependencies automatically.
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

            SR_LOG_DEBUG("node added with automatic dependency tracking (r: {}, w: {}).", read_access.size(), write_access.size());
            
            impl_->nodes.push_back(std::move(node));
        }

        /**
         * @brief executes the entire graph using the task scheduler.
         */
        // TODO: refactor this function to be non-blocking and return a future immediately.
        sycl::event TaskGraph::execute() 
        {
            SR_LOG_INFO("executing dag with {} nodes.", impl_->nodes.size());

            if(impl_->nodes.empty()) return sycl::event{};

            /** @brief setup local scheduler for this execution. */
            SushiRuntime::Scheduler::TaskScheduler scheduler(ctx_);

            /** @brief identify roots and create a sentinel node for completion. */
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

            /** @brief submit all root nodes to start execution. */
            for(auto* root : roots) 
            {
                scheduler.submit(root);
            }

            /** @brief launch worker threads to help with task processing. */
            // TODO: use a persistent thread pool to avoid thread creation overhead.
            std::vector<std::thread> workers;
            size_t concurrency = std::thread::hardware_concurrency();
            if(concurrency == 0) concurrency = 1;

            for(size_t i=1; i<concurrency; ++i) 
            {
                workers.emplace_back([&scheduler, i](){
                    try 
                    {
                        scheduler.dispatch_loop(i);
                    }
                    catch(const std::exception& e)
                    {
                        SR_LOG_ERROR("worker thread {} crashed: {}", i, e.what());
                    }
                });
            }
            
            /** @brief join execution loop on the main thread. */
            scheduler.dispatch_loop(0);
            
            for(auto& t : workers) 
            {
                if(t.joinable()) t.join();
            }

            SR_LOG_INFO("dag execution completed.");
            return sycl::event{};
        }

    } // namespace Graph
} // namespace SushiRuntime