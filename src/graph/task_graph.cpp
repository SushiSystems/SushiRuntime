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
#include <memory>
#include <stdexcept>
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
            /** @brief Mutex to protect nodes vector from concurrent access. */
            std::mutex nodes_mutex;
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
         * @brief Adds a device task (kernel/transfer) with manual event dependencies.
         */
        void TaskGraph::add_node(TaskWork work, const std::vector<sycl::event>& dependencies) 
        {
            auto node = make_sushi<Node>();
            node->work = std::move(work);
            node->sycl_deps = dependencies;
            
            SR_LOG_DEBUG("Node added with {} manual dependencies.", dependencies.size());

            std::lock_guard<std::mutex> lock(impl_->nodes_mutex);
            impl_->nodes.push_back(std::move(node));
        }

        /**
         * @brief Adds a device task and resolves USM data dependencies automatically.
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
            
            std::lock_guard<std::mutex> lock(impl_->nodes_mutex);
            impl_->nodes.push_back(std::move(node));
        }

        /**
         * @brief Adds a semantic task into the graph with explicit device and host fallback work.
         */
        void TaskGraph::add_task(const TaskMetadata& meta,
                                 const std::vector<void*>& read_access,
                                 const std::vector<void*>& write_access,
                                 TaskWork device_work,
                                 HostWork fallback_work,
                                 const std::vector<sycl::event>& dependencies)
        {
            auto node = make_sushi<Node>();
            node->metadata = meta;
            node->work = std::move(device_work);
            node->host_work = std::move(fallback_work);
            node->sycl_deps = dependencies;

            const bool has_device_work = static_cast<bool>(node->work);
            const bool has_host_work = static_cast<bool>(node->host_work);

            if (meta.task_type == TaskType::CUSTOM_HOST) 
            {
                node->mode = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;
            }
            else if (has_device_work)
            {
                node->mode = SushiRuntime::Scheduler::Task::ExecutionMode::KERNEL;
            }
            else
            {
                node->mode = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;
                if (meta.task_type != TaskType::CUSTOM_HOST)
                {
                    SR_LOG_WARN("Semantic task '{}' has no device work; falling back to host execution.", meta.name);
                }
            }

            if (!has_device_work && !has_host_work)
            {
                SR_LOG_WARN("Semantic task '{}' has neither device work nor host fallback.", meta.name);
            }
            
            // Resolve USM dependencies for reads and writes
            for(void* ptr : read_access) 
            {
                if (ptr) impl_->tracker.resolve(node.get(), ptr, false);
            }
            for(void* ptr : write_access) 
            {
                if (ptr) impl_->tracker.resolve(node.get(), ptr, true);
            }

            SR_LOG_DEBUG("Semantic Node '{}' (Type: {}) added (R: {}, W: {}).", 
                         meta.name, static_cast<int>(meta.task_type), 
                         read_access.size(), write_access.size());
            
            std::lock_guard<std::mutex> lock(impl_->nodes_mutex);
            impl_->nodes.push_back(std::move(node));
        }

        /**
         * @brief Adds a semantic task into the graph with host-side fallback only.
         */
        void TaskGraph::add_task(const TaskMetadata& meta,
                                 const std::vector<void*>& read_access,
                                 const std::vector<void*>& write_access,
                                 HostWork fallback_work,
                                 const std::vector<sycl::event>& dependencies)
        {
            add_task(meta,
                     read_access,
                     write_access,
                     TaskWork{},
                     std::move(fallback_work),
                     dependencies);
        }

        /**
         * @brief Analyzes dependencies and compiles the graph for execution.
         * 
         * This process links leaves to a sentinel completion node, finds roots,
         * and transfers ownership to a CompiledGraph. It does NOT submit tasks
         * to the hardware or block execution.
         * 
         * @return CompiledGraph The compiled graph ready to run.
         */
        SushiRuntime::Execution::CompiledGraph TaskGraph::compile() 
        {
            SR_LOG_INFO("Compiling DAG with {} nodes.", impl_->nodes.size());

            SushiRuntime::Execution::CompiledGraph graph;
            
            std::lock_guard<std::mutex> lock(impl_->nodes_mutex);
            if(impl_->nodes.empty()) return graph;

            auto sentinel = make_sushi<Node>();

            for (auto& node : impl_->nodes) 
            {
                if (!node) continue;
                // find graph roots (nodes with no incoming dependencies)
                if (node->in_degree.load(std::memory_order_relaxed) == 0) 
                {
                    graph.roots.push_back(node.get());
                }
                
                // link graph leaves to the sentinel node
                if (node->inline_successor_count.load(std::memory_order_relaxed) == 0) 
                {
                    impl_->tracker.add_dependency(node.get(), sentinel.get());
                }
            }

            if (graph.roots.empty())
            {
                SR_LOG_ERROR("Task graph has no root nodes. The graph may contain a cycle or unresolved dependencies.");
                throw std::runtime_error("Task graph has no root nodes.");
            }

            // Transfer ownership to CompiledGraph
            graph.sentinel = std::move(sentinel);

            graph.all_nodes = std::move(impl_->nodes);
            
            impl_->tracker.reset();

            SR_LOG_INFO("DAG compilation completed.");
            return graph;
        }

    } // namespace Graph
} // namespace SushiRuntime
