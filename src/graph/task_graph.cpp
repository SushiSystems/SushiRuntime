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

#include <algorithm>
#include <future>
#include <memory>
#include <stdexcept>
#include <atomic>
#include <vector>
#include <unordered_map>
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

                // The node carried op_id through a tracked add, so its offload state
                // already exists; offload() returns it without a fresh allocation.
                OffloadState& off = n->offload();

                // A double-buffered node resolves its handles from live pointers at
                // dispatch, so do not bake them here. Mark it offloadable only when
                // every buffer it currently touches is registered; otherwise leave it
                // local (the host fallback still runs).
                if (off.live_reads || off.live_writes)
                {
                    bool live_ok = true;
                    if (off.live_reads)
                        for (void* p : *off.live_reads)
                            if (!ctx.distributed_buffer(p)) { live_ok = false; break; }
                    if (live_ok && off.live_writes)
                        for (void* p : *off.live_writes)
                            if (!ctx.distributed_buffer(p)) { live_ok = false; break; }
                    if (!live_ok) continue;

                    off.offloadable = true;
                    off.remote_op   = n->metadata.op_id;
                    continue;
                }

                std::vector<SushiRuntime::Distributed::BufferHandle> ins, outs;
                bool ok = true;

                for (void* p : off.reads)
                {
                    auto h = ctx.distributed_buffer(p);
                    if (!h) { ok = false; break; }
                    ins.push_back(*h);
                }
                if (ok) for (void* p : off.writes)
                {
                    auto h = ctx.distributed_buffer(p);
                    if (!h) { ok = false; break; }
                    outs.push_back(*h);
                }
                if (!ok) continue;

                off.offloadable = true;
                off.remote_op   = n->metadata.op_id;
                off.in_handles  = std::move(ins);
                off.out_handles = std::move(outs);
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
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                 const std::vector<sycl::event>& dependencies)
        {
            add_node(std::move(work), read_access, write_access, nullptr, dependencies);
        }

        /**
         * @brief Adds a device task with automatic tracking and a late-binding block.
         *
         * @param work The task payload to execute.
         * @param read_access A list of USM regions the task reads from.
         * @param write_access A list of USM regions the task writes to.
         * @param bindings Shared per-step controls; null behaves like the plain overload.
         * @param dependencies An optional list of manual SYCL dependencies.
         */
        void TaskGraph::add_node(TaskWork work,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                 std::shared_ptr<LateBindings> bindings,
                                 const std::vector<sycl::event>& dependencies)
        {
            auto node = make_sushi<Node>();
            node->work = std::move(work);
            node->sycl_deps = dependencies;
            node->bindings_ = std::move(bindings);

            // Resolve USM dependencies for reads and writes
            for (const auto& r : read_access)
                if (r.base) impl_->tracker.resolve(node.get(), r, false);

            for (const auto& w : write_access)
                if (w.base) impl_->tracker.resolve(node.get(), w, true);

            // Retain the access regions on the node, exactly as add_task does. The
            // scheduler uses them to route a KERNEL submit to the queue that owns
            // the USM; without them a kernel can run on a device whose context does
            // not own the data — a cross-context illegal access on heterogeneous
            // setups (e.g. a GPU kernel touching CPU-context USM).
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            // The distributed coherence model is whole-buffer, so offload resolves
            // handles from the owning base of each region (sub-region offload is a
            // later refinement).
            for (const auto& r : read_access)  node->offload().reads.push_back(r.base);
            for (const auto& w : write_access) node->offload().writes.push_back(w.base);
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
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
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
            for (const auto& r : read_access)
                if (r.base) impl_->tracker.resolve(node.get(), r, false);

            for (const auto& w : write_access)
                if (w.base) impl_->tracker.resolve(node.get(), w, true);

            // Retain the access regions on the node (every build). The scheduler
            // uses them to route a KERNEL submit to the queue that owns the USM
            // (avoiding a cross-context illegal access on heterogeneous setups).
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            // The distributed layer resolves the owning base of each region to a
            // serializable buffer handle for potential offload (whole-buffer
            // coherence; sub-region offload is a later refinement).
            for (const auto& r : read_access)  node->offload().reads.push_back(r.base);
            for (const auto& w : write_access) node->offload().writes.push_back(w.base);
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
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
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
         * @brief Adds an offloadable task whose touched buffers rotate every step.
         *
         * @param meta Metadata carrying the offloadable op_id.
         * @param live_reads Shared list of the buffers read this step.
         * @param live_writes Shared list of the buffers written this step.
         * @param fallback_work Host fallback run when the task is not offloaded.
         * @param dependencies An optional list of manual SYCL dependencies.
         */
        void TaskGraph::add_offload_dynamic(const TaskMetadata& meta,
                                            std::shared_ptr<std::vector<void*>> live_reads,
                                            std::shared_ptr<std::vector<void*>> live_writes,
                                            HostWork fallback_work,
                                            const std::vector<sycl::event>& dependencies)
        {
            auto node = make_sushi<Node>();
            node->metadata  = meta;
            node->host_work = std::move(fallback_work);
            node->sycl_deps = dependencies;
            node->mode      = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;

            // Snapshot the current pointers for dependency ordering and queue routing
            // (both reason about a single compiled graph, where the snapshot holds).
            const std::vector<void*> reads  = live_reads  ? *live_reads  : std::vector<void*>{};
            const std::vector<void*> writes = live_writes ? *live_writes : std::vector<void*>{};

            // A double-buffered offload node touches whole allocations that rotate
            // each step, so each live pointer maps to a whole-buffer region.
            for (void* ptr : reads)
                if (ptr) impl_->tracker.resolve(node.get(), ptr, false);
            for (void* ptr : writes)
                if (ptr) impl_->tracker.resolve(node.get(), ptr, true);

            node->io_reads_.assign(reads.begin(), reads.end());
            node->io_writes_.assign(writes.begin(), writes.end());

#ifdef SR_DISTRIBUTED
            auto& off = node->offload();
            off.reads   = reads;
            off.writes  = writes;
            // The live lists drive dispatch-time handle resolution so the node
            // follows the double-buffer swap without recompiling.
            off.live_reads   = std::move(live_reads);
            off.live_writes  = std::move(live_writes);
#endif

            impl_->push_node_lock_free(std::move(node));
        }

        /**
         * @brief Adds a pure-CPU task run natively on a worker thread.
         *
         * @param read_access Regions this task reads from.
         * @param write_access Regions this task writes to.
         * @param work Callable run once on a worker thread; submits no SYCL work.
         * @param dependencies An optional list of manual SYCL dependencies.
         */
        void TaskGraph::add_native(const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                   const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                   std::function<void()> work,
                                   const std::vector<sycl::event>& dependencies)
        {
            auto node = make_sushi<Node>();
            node->native_work          = std::move(work);
            node->submits_device_work_ = false;
            node->mode                 = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;
            node->sycl_deps            = dependencies;

            for (const auto& r : read_access)
                if (r.base) impl_->tracker.resolve(node.get(), r, false);

            for (const auto& w : write_access)
                if (w.base) impl_->tracker.resolve(node.get(), w, true);

            // Retain regions for dependency identity. Queue routing is unused for a
            // native task (it acquires no queue), but the lists stay consistent with
            // every other node and keep the distributed handle resolution working.
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            for (const auto& r : read_access)  node->offload().reads.push_back(r.base);
            for (const auto& w : write_access) node->offload().writes.push_back(w.base);
#endif

            SR_LOG_DEBUG("Native CPU node added (R: {}, W: {}).",
                         read_access.size(), write_access.size());

            impl_->push_node_lock_free(std::move(node));
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
                // Drain the tracker BEFORE the throw. `graph` (and every Node it
                // owns) is destroyed as this exception unwinds, but the tracker's
                // AccessNodes still hold raw task pointers into those nodes. This
                // TaskGraph survives a failed compile(), so a later add_node() ->
                // resolve() would traverse the history and add_dependency() to
                // freed pointers — a use-after-free write. reset() retires the
                // AccessNodes and clears the shard tails, so the next resolve()
                // starts from an empty (and valid) history.
                impl_->tracker.reset();
                throw std::runtime_error("Task graph has no root nodes.");
            }

            // Cycle guard. A non-empty root set is necessary but NOT sufficient: a
            // graph can have a valid root AND a disjoint dependency cycle whose nodes
            // never reach in_degree 0, so they are never dispatched and completion
            // never fires — the run hangs forever on the sentinel. Simulate the
            // runtime drain (Kahn's algorithm) over a shadow of the in-degrees; if
            // any node is unreachable it sits in such a cycle. This is O(N+E) but
            // paid once at compile, amortized over every replay of the plan.
            {
                std::unordered_map<Node*, int32_t> shadow_indeg;
                shadow_indeg.reserve(graph.all_nodes.size());
                for (auto& n : graph.all_nodes)
                    shadow_indeg.emplace(n.get(),
                        n->in_degree.load(std::memory_order_relaxed));

                std::vector<Node*> ready(graph.roots.begin(), graph.roots.end());
                size_t drained = 0;

                auto relax_successor = [&](Node* succ)
                {
                    auto it = shadow_indeg.find(succ);
                    if (it == shadow_indeg.end()) return;   // sentinel / external
                    if (--(it->second) == 0)
                        ready.push_back(succ);
                };

                while (!ready.empty())
                {
                    Node* n = ready.back();
                    ready.pop_back();
                    ++drained;

                    const int32_t inl = std::min<int32_t>(
                        n->inline_successor_count.load(std::memory_order_relaxed),
                        Node::MAX_INLINE_SUCCESSORS);
                    for (int32_t i = 0; i < inl; ++i)
                        if (n->inline_successors[i]) relax_successor(n->inline_successors[i]);

                    for (auto* s = n->successor_list.load(std::memory_order_relaxed);
                         s; s = s->next)
                        if (s->target) relax_successor(s->target);

                    for (auto* succ : n->region.boundary_successors)
                        if (succ) relax_successor(succ);
                }

                if (drained != graph.all_nodes.size())
                {
                    SR_LOG_ERROR("Task graph contains a dependency cycle: {} of {} "
                                 "nodes are unreachable and would hang the run.",
                                 graph.all_nodes.size() - drained,
                                 graph.all_nodes.size());
                    impl_->tracker.reset(); // same UAF-avoidance as the no-roots path
                    throw std::runtime_error(
                        "Task graph contains a dependency cycle (unschedulable nodes).");
                }
            }

            graph.sentinel = std::move(sentinel);
            impl_->tracker.reset(); // Safely clear the lock-free history

#ifdef SR_DISTRIBUTED
            stamp_offloadable(graph, impl_->context);
#endif

            SR_LOG_INFO("DAG compilation completed successfully.");
            return graph;
        }

        /**
         * @brief Hands off the built nodes without resolving roots/leaves or a sentinel.
         *
         * @return The nodes in insertion order; the insertion stacks are left empty.
         */
        std::vector<sushi_ptr<Node>> TaskGraph::take_nodes()
        {
            std::vector<sushi_ptr<Node>> out;

            NodeEntry* head = impl_->all_nodes.exchange(nullptr, std::memory_order_acquire);
            while (head)
            {
                out.push_back(std::move(head->node));
                NodeEntry* next = head->next;
                delete head;
                head = next;
            }

            // The root/leaf candidate stacks and the tracker history only matter for
            // compile(); drop them so this graph is a clean, reusable builder.
            RawNodeEntry* pr = impl_->potential_roots.exchange(nullptr, std::memory_order_acquire);
            while (pr) { RawNodeEntry* n = pr->next; delete pr; pr = n; }

            RawNodeEntry* pl = impl_->potential_leaves.exchange(nullptr, std::memory_order_acquire);
            while (pl) { RawNodeEntry* n = pl->next; delete pl; pl = n; }

            impl_->tracker.reset();

            // push_node_lock_free is a LIFO stack, so the drain above is in reverse
            // insertion order. The composer re-resolves dependencies in adoption
            // order, so it must see nodes as the caller added them; restore that.
            std::reverse(out.begin(), out.end());
            return out;
        }

    } // namespace Graph
} // namespace SushiRuntime