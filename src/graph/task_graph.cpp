/**************************************************************************/
/* task_graph.cpp                                                         */
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

            /**
             * @brief Constructs the internal implementation.
             *
             * @param ctx The runtime context used for execution.
             */
            Impl(RuntimeContext& ctx) : context(ctx) {}

            /**
             * @brief Destroys the internal implementation.
             *
             * Safely cleans up remaining lock-free entries if compilation was not completed.
             */
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
             *
             * Eliminates the need for any std::mutex during graph construction.
             *
             * @param node The node to insert.
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

#ifdef SR_DISTRIBUTED
        /**
         * @brief Flags nodes that may run on a remote worker (Master mode only).
         *
         * A node is offloadable iff it carries a registered OpID the cluster can
         * run AND every buffer it touches has a serializable handle. Anything
         * else — and every node in Standalone/Worker mode — stays local, so the
         * scheduler's offload branch is a no-op for it.
         *
         * @param graph The compiled graph to process.
         * @param ctx The current runtime context.
         */
        static void stamp_offloadable(SushiRuntime::Execution::CompiledGraph& graph, RuntimeContext& ctx)
        {
            if (ctx.distributed_role() != RuntimeContext::RuntimeRole::Master)
                return;

            for (auto& np : graph.all_nodes)
            {
                Node* n = np.get();
                if (!n || n->metadata.op_id.value == 0) continue;
                if (!ctx.distributed_supports(n->metadata.op_id)) continue;

                std::vector<SushiRuntime::Distributed::BufferHandle> ins, outs;
                bool ok = true;

                for (void* p : n->dist_reads_)
                {
                    auto h = ctx.distributed_buffer(p);
                    if (!h) { ok = false; break; }
                    ins.push_back(*h);
                }
                if (ok) for (void* p : n->dist_writes_)
                {
                    auto h = ctx.distributed_buffer(p);
                    if (!h) { ok = false; break; }
                    outs.push_back(*h);
                }
                if (!ok) continue;

                n->offloadable_  = true;
                n->remote_op_    = n->metadata.op_id;
                n->in_handles_   = std::move(ins);
                n->out_handles_  = std::move(outs);
            }
        }
#endif

        /**
         * @brief Constructs a new TaskGraph.
         *
         * @param ctx The runtime context used to execute tasks.
         */
        TaskGraph::TaskGraph(RuntimeContext& ctx) : impl_(make_sushi<Impl>(ctx))
        {
            SR_LOG_DEBUG("Lock-Free O(1) TaskGraph initialized.");
        }

        /**
         * @brief Destroys the TaskGraph.
         */
        TaskGraph::~TaskGraph() = default;

        /**
         * @brief Adds a device task (kernel/transfer) with manual event dependencies.
         *
         * @param work The task payload to execute.
         * @param dependencies An optional list of manual SYCL dependencies.
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
         *
         * @param work The task payload to execute.
         * @param read_access A list of USM pointers the task reads from.
         * @param write_access A list of USM pointers the task writes to.
         * @param dependencies An optional list of manual SYCL dependencies.
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

            // Retain the access lists on the node, exactly as add_task does. The
            // scheduler uses them to route a KERNEL submit to the queue that owns
            // the USM; without them a kernel can run on a device whose context does
            // not own the data — a cross-context illegal access on heterogeneous
            // setups (e.g. a GPU kernel touching CPU-context USM).
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            node->dist_reads_  = read_access;
            node->dist_writes_ = write_access;
#endif

            SR_LOG_DEBUG("Node added with automatic dependency tracking (R: {}, W: {}).", read_access.size(), write_access.size());

            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node));
        }

        /**
         * @brief Adds a semantic task into the graph with explicit device and host fallback work.
         *
         * @param meta Metadata describing the task.
         * @param read_access A list of USM pointers the task reads from.
         * @param write_access A list of USM pointers the task writes to.
         * @param device_work The preferred device-side execution workload.
         * @param fallback_work A host-side alternative or fallback workload.
         * @param dependencies An optional list of manual SYCL dependencies.
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

            // Retain the access lists on the node (every build). The scheduler
            // uses them to route a KERNEL submit to the queue that owns the USM
            // (avoiding a cross-context illegal access on heterogeneous setups).
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            // The distributed layer resolves these to serializable buffer handles
            // for potential offload; same pointers, kept under the existing names.
            node->dist_reads_  = read_access;
            node->dist_writes_ = write_access;
#endif

            SR_LOG_DEBUG("Semantic Node '{}' (Type: {}) added (R: {}, W: {}).",
                         meta.name, static_cast<int>(meta.task_type), 
                         read_access.size(), write_access.size());
            
            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node));
        }

        /**
         * @brief Adds a semantic task into the graph with host-side fallback only.
         *
         * @param meta Metadata describing the task.
         * @param read_access A list of USM pointers the task reads from.
         * @param write_access A list of USM pointers the task writes to.
         * @param fallback_work A host-side workload to execute.
         * @param dependencies An optional list of manual SYCL dependencies.
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
         *
         * Bypasses the heavy O(N) full graph traversal by exclusively processing 
         * nodes that were incrementally tagged as potential roots and leaves during insertion.
         *
         * @return SushiRuntime::Execution::CompiledGraph The compiled graph ready to run.
         * @throws std::runtime_error If the task graph contains no root nodes.
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

#ifdef SR_DISTRIBUTED
            stamp_offloadable(graph, impl_->context);
#endif

            SR_LOG_INFO("DAG compilation completed successfully.");
            return graph;
        }

    } // namespace Graph
} // namespace SushiRuntime