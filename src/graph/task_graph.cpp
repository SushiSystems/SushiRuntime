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
#include <atomic>
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
         * @brief Lock-free linked list entry for owning sushi_ptr objects.
         */
        struct NodeEntry 
        {
            sushi_ptr<Node> node;
            NodeEntry* next;
        };

        /**
         * @brief Lock-free linked list entry for non-owning raw pointers (used for quick tracking).
         */
        struct RawNodeEntry 
        {
            Node* node;
            RawNodeEntry* next;
        };

        /**
         * @brief Private implementation of the TaskGraph utilizing O(1) Lock-Free architecture.
         */
        struct TaskGraph::Impl : public Core::RefCounted
        {
            /** @brief Lock-free stack holding all nodes to manage their lifetime securely. */
            std::atomic<NodeEntry*> all_nodes{nullptr};
            
            /** @brief Lock-free stack tracking nodes that are potentially roots. */
            std::atomic<RawNodeEntry*> potential_roots{nullptr};
            
            /** @brief Lock-free stack tracking nodes that are potentially leaves. */
            std::atomic<RawNodeEntry*> potential_leaves{nullptr};

            /** @brief Tracker to handle automatic dependencies seamlessly. */
            DependencyTracker tracker;
            
            /** @brief Reference to the runtime context. */
            RuntimeContext& context;

            Impl(RuntimeContext& ctx) : context(ctx) {}

            ~Impl() 
            {
                // Safely clean up remaining lock-free entries in case compile() was never called.
                NodeEntry* head = all_nodes.exchange(nullptr, std::memory_order_acquire);
                while (head) 
                {
                    NodeEntry* next = head->next;
                    delete head;
                    head = next;
                }

                RawNodeEntry* pr = potential_roots.exchange(nullptr, std::memory_order_acquire);
                while (pr) 
                { 
                    RawNodeEntry* next = pr->next; 
                    delete pr; 
                    pr = next; 
                }

                RawNodeEntry* pl = potential_leaves.exchange(nullptr, std::memory_order_acquire);
                while (pl) 
                { 
                    RawNodeEntry* next = pl->next; 
                    delete pl; 
                    pl = next; 
                }
            }

            /**
             * @brief Pushes a node into the lock-free tracking stacks in O(1) time.
             * Eliminates the need for any std::mutex during graph construction.
             */
            void push_node_lock_free(sushi_ptr<Node> node)
            {
                Node* raw_ptr = node.get();
                
                // 1. Push to main lifetime-management stack
                NodeEntry* entry = new NodeEntry{std::move(node), nullptr};
                NodeEntry* old_head = all_nodes.load(std::memory_order_relaxed);
                do 
                {
                    entry->next = old_head;
                } 
                while (!all_nodes.compare_exchange_weak(old_head, entry, 
                                                        std::memory_order_release, 
                                                        std::memory_order_relaxed));

                // 2. Push to potential roots stack
                RawNodeEntry* root_entry = new RawNodeEntry{raw_ptr, nullptr};
                RawNodeEntry* old_root = potential_roots.load(std::memory_order_relaxed);
                do 
                {
                    root_entry->next = old_root;
                } 
                while (!potential_roots.compare_exchange_weak(old_root, root_entry, 
                                                              std::memory_order_release, 
                                                              std::memory_order_relaxed));

                // 3. Push to potential leaves stack
                RawNodeEntry* leaf_entry = new RawNodeEntry{raw_ptr, nullptr};
                RawNodeEntry* old_leaf = potential_leaves.load(std::memory_order_relaxed);
                do 
                {
                    leaf_entry->next = old_leaf;
                } 
                while (!potential_leaves.compare_exchange_weak(old_leaf, leaf_entry, 
                                                               std::memory_order_release, 
                                                               std::memory_order_relaxed));
            }
        };

        TaskGraph::TaskGraph(RuntimeContext& ctx) : impl_(make_sushi<Impl>(ctx)) 
        {
            SR_LOG_DEBUG("Lock-Free O(1) TaskGraph initialized.");
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

            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node));
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
            
            // Resolve USM dependencies for reads and writes
            for(void* ptr : read_access) 
                if (ptr) impl_->tracker.resolve(node.get(), ptr, false);
            
            for(void* ptr : write_access) 
                if (ptr) impl_->tracker.resolve(node.get(), ptr, true);

            SR_LOG_DEBUG("Node added with automatic dependency tracking (R: {}, W: {}).", read_access.size(), write_access.size());
            
            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node));
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

            // Execution mode is decided by capability + explicit intent:
            //   * KERNEL — a device_work lambda is present and the task is not
            //              explicitly marked host-only (CUSTOM_HOST).
            //   * HOST   — otherwise. A host_work lambda is fully valid, intentional
            //              work and may itself submit device kernels; the runtime
            //              cannot introspect the std::function, so choosing host
            //              mode here is NOT an error and must not warn.
            if (has_device_work && meta.task_type != TaskType::CUSTOM_HOST)
            {
                node->mode = SushiRuntime::Scheduler::Task::ExecutionMode::KERNEL;
            }
            else
            {
                node->mode = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;
            }

            // The only genuine defect is a task that carries no executable work at
            // all. Everything else is a deliberate, supported configuration.
            if (!has_device_work && !has_host_work)
            {
                SR_LOG_WARN("Semantic task '{}' has neither device work nor host fallback — it will be a no-op.", meta.name);
            }
            else if (!has_device_work)
            {
                SR_LOG_DEBUG("Semantic task '{}' scheduled as host execution (its host_work may still submit device kernels).", meta.name);
            }
            
            // Resolve USM dependencies utilizing Lock-Free Access History
            for(void* ptr : read_access) 
                if (ptr) impl_->tracker.resolve(node.get(), ptr, false);
            
            for(void* ptr : write_access) 
                if (ptr) impl_->tracker.resolve(node.get(), ptr, true);

            SR_LOG_DEBUG("Semantic Node '{}' (Type: {}) added (R: {}, W: {}).", 
                         meta.name, static_cast<int>(meta.task_type), 
                         read_access.size(), write_access.size());
            
            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node));
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
         * @brief Analyzes dependencies and compiles the graph for execution in O(1) logical time.
         * * Bypasses the heavy O(N) full graph traversal by exclusively processing 
         * nodes that were incrementally tagged as potential roots and leaves during insertion.
         * * @return CompiledGraph The compiled graph ready to run.
         */
        SushiRuntime::Execution::CompiledGraph TaskGraph::compile() 
        {
            SushiRuntime::Execution::CompiledGraph graph;
            
            // Acquire the entire lifetime stack atomically
            NodeEntry* head = impl_->all_nodes.exchange(nullptr, std::memory_order_acquire);
            if (!head) 
            {
                return graph; // Empty graph
            }

            auto sentinel = make_sushi<Node>();

            // Transfer node ownership to CompiledGraph securely
            while (head) 
            {
                graph.all_nodes.push_back(std::move(head->node));
                NodeEntry* next = head->next;
                delete head;
                head = next;
            }

            // 1. Resolve true Root nodes from the potential candidates
            RawNodeEntry* p_root = impl_->potential_roots.exchange(nullptr, std::memory_order_acquire);
            while (p_root) 
            {
                if (p_root->node->in_degree.load(std::memory_order_relaxed) == 0) 
                    graph.roots.push_back(p_root->node);
                
                RawNodeEntry* next = p_root->next;
                delete p_root;
                p_root = next;
            }

            // 2. Resolve true Leaf nodes and link them to the Sentinel
            RawNodeEntry* p_leaf = impl_->potential_leaves.exchange(nullptr, std::memory_order_acquire);
            while (p_leaf) 
            {
                // A node is a true leaf only if it has zero outgoing edges (no successors)
                bool is_leaf = (p_leaf->node->inline_successor_count.load(std::memory_order_relaxed) == 0) &&
                               (p_leaf->node->successor_list.load(std::memory_order_relaxed) == nullptr);
                               
                if (is_leaf) 
                    impl_->tracker.add_dependency(p_leaf->node, sentinel.get());

                RawNodeEntry* next = p_leaf->next;
                delete p_leaf;
                p_leaf = next;
            }

            if (graph.roots.empty())
            {
                SR_LOG_ERROR("Task graph has no root nodes. The graph may contain a cycle or unresolved dependencies.");
                throw std::runtime_error("Task graph has no root nodes.");
            }

            graph.sentinel = std::move(sentinel);
            impl_->tracker.reset(); // Safely clear the lock-free history

            SR_LOG_INFO("DAG compilation completed successfully.");
            return graph;
        }

    } // namespace Graph
} // namespace SushiRuntime