/**************************************************************************/
/* task_graph.cpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file task_graph.cpp
 * @brief Builds the task graph from added nodes and compiles it into an executable plan.
 * @author Mustafa Garip
 */

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <sycl/sycl.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/dependency_tracker.hpp>
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
             * @brief Source of fresh registration indices, shared across add_* calls.
             *
             * add_node()/add_task()/add_native()/add_offload_dynamic() run on the
             * same lock-free contract as the rest of graph construction, so a plain
             * counter would race under concurrent callers; fetch_add makes each draw
             * atomic and distinct regardless of who is calling.
             */
            std::atomic<std::uint32_t> registration_counter_{0};

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
             * @brief Pushes a node onto the lock-free tracking stacks (src/graph/README.md).
             * @param node The node to insert.
             * @param registration Index to stamp on the node; absent draws a fresh one.
             */
            void push_node_lock_free(sushi_ptr<Node> node,
                                     std::optional<std::uint32_t> registration = std::nullopt)
            {
                // Stamped before publishing, so every reader sees a formed index.
                node->registration_index = registration
                    ? *registration
                    : registration_counter_.fetch_add(1, std::memory_order_relaxed);

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
         * @brief Flags the nodes a remote worker may run; Master mode only (src/graph/README.md).
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

                // offload() allocates nothing after a tracked add (src/graph/README.md).
                OffloadState& off = n->offload();

                // A double-buffered node bakes no handles here (src/graph/README.md).
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
         * @param registration Registration index to stamp on this node instead of
         *                     drawing a fresh one.
         */
        void TaskGraph::add_node(TaskWork work, const std::vector<sycl::event>& dependencies,
                                 std::optional<std::uint32_t> registration)
        {
            auto node = make_sushi<Node>();
            node->work = std::move(work);
            node->sycl_deps = dependencies;

            SR_LOG_DEBUG("Node added with {} manual dependencies.", dependencies.size());

            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node), registration);
        }

        /**
         * @brief Adds a device task and resolves USM data dependencies automatically.
         * @param registration Index to stamp on this node; absent draws a fresh one.
         */
        void TaskGraph::add_node(TaskWork work,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                 const std::vector<sycl::event>& dependencies,
                                 std::optional<std::uint32_t> registration)
        {
            add_node(std::move(work), read_access, write_access, nullptr, dependencies, registration);
        }

        /**
         * @brief Adds a device task with automatic tracking and a late-binding block.
         * @param bindings Shared per-step controls; null behaves like the plain overload.
         * @param registration Index to stamp on this node; absent draws a fresh one.
         */
        void TaskGraph::add_node(TaskWork work,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                 std::shared_ptr<LateBindings> bindings,
                                 const std::vector<sycl::event>& dependencies,
                                 std::optional<std::uint32_t> registration)
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

            // The regions stay on the node for queue routing (src/graph/README.md).
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            // Offload resolves handles from each region's owning base (src/graph/README.md).
            for (const auto& r : read_access)  node->offload().reads.push_back(r.base);
            for (const auto& w : write_access) node->offload().writes.push_back(w.base);
#endif

            SR_LOG_DEBUG("Node added with automatic dependency tracking (R: {}, W: {}).", read_access.size(), write_access.size());

            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node), registration);
        }

        /**
         * @brief Adds a semantic task with explicit device work and a host fallback.
         * @param device_work The preferred device-side workload; empty selects host mode.
         * @param registration Index to stamp on this node; absent draws a fresh one.
         */
        void TaskGraph::add_task(const TaskMetadata& meta,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                 TaskWork device_work,
                                 HostWork fallback_work,
                                 const std::vector<sycl::event>& dependencies,
                                 std::optional<std::uint32_t> registration)
        {
            auto node = make_sushi<Node>();
            node->metadata = meta;
            node->work = std::move(device_work);
            node->host_work = std::move(fallback_work);
            node->sycl_deps = dependencies;

            const bool has_device_work = static_cast<bool>(node->work);
            const bool has_host_work = static_cast<bool>(node->host_work);

            // KERNEL needs device work and a type other than CUSTOM_HOST (src/graph/README.md).
            if (has_device_work && meta.task_type != TaskType::CUSTOM_HOST)
            {
                node->mode = SushiRuntime::Scheduler::Task::ExecutionMode::KERNEL;
            }
            else
            {
                node->mode = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;
            }

            // A task with no executable work is the only defect (src/graph/README.md).
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

            // The regions stay on the node for queue routing (src/graph/README.md).
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            // Offload resolves handles from each region's owning base (src/graph/README.md).
            for (const auto& r : read_access)  node->offload().reads.push_back(r.base);
            for (const auto& w : write_access) node->offload().writes.push_back(w.base);
#endif

            SR_LOG_DEBUG("Semantic Node '{}' (Type: {}) added (R: {}, W: {}).",
                         meta.name, static_cast<int>(meta.task_type), 
                         read_access.size(), write_access.size());
            
            // O(1) Lock-Free insertion
            impl_->push_node_lock_free(std::move(node), registration);
        }

        /**
         * @brief Adds a semantic task that carries host-side work only.
         * @param registration Index to stamp on this node; absent draws a fresh one.
         */
        void TaskGraph::add_task(const TaskMetadata& meta,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                 const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                 HostWork fallback_work,
                                 const std::vector<sycl::event>& dependencies,
                                 std::optional<std::uint32_t> registration)
        {
            add_task(meta,
                     read_access,
                     write_access,
                     TaskWork{},
                     std::move(fallback_work),
                     dependencies,
                     registration);
        }

        /**
         * @brief Adds an offloadable task whose touched buffers rotate every step.
         * @param meta Metadata carrying the offloadable op_id.
         * @param registration Index to stamp on this node; absent draws a fresh one.
         */
        void TaskGraph::add_offload_dynamic(const TaskMetadata& meta,
                                            std::shared_ptr<std::vector<void*>> live_reads,
                                            std::shared_ptr<std::vector<void*>> live_writes,
                                            HostWork fallback_work,
                                            const std::vector<sycl::event>& dependencies,
                                            std::optional<std::uint32_t> registration)
        {
            auto node = make_sushi<Node>();
            node->metadata  = meta;
            node->host_work = std::move(fallback_work);
            node->sycl_deps = dependencies;
            node->mode      = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;

            // The snapshot orders dependencies and routes the queue (src/graph/README.md).
            const std::vector<void*> reads  = live_reads  ? *live_reads  : std::vector<void*>{};
            const std::vector<void*> writes = live_writes ? *live_writes : std::vector<void*>{};

            // Each live pointer maps to a whole-buffer region (src/graph/README.md).
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
            // The live lists resolve handles at dispatch (src/graph/README.md).
            off.live_reads   = std::move(live_reads);
            off.live_writes  = std::move(live_writes);
#endif

            impl_->push_node_lock_free(std::move(node), registration);
        }

        /**
         * @brief Names the node most recently added to this graph (src/graph/README.md).
         * @param name Persistent label, stored not copied; null is a no-op.
         */
        void TaskGraph::name_last_node(const char* name) noexcept
        {
            if (!name)
                return;
            NodeEntry* head = impl_->all_nodes.load(std::memory_order_relaxed);
            if (head && head->node)
                head->node->metadata.name = name;
        }

        /**
         * @brief Stamps the element count on the node most recently added.
         *
         * Mirrors name_last_node(): all_nodes is a LIFO stack, so its head is the
         * last node pushed, and a call with no node yet added is a no-op.
         */
        void TaskGraph::width_last_node(std::size_t width) noexcept
        {
            NodeEntry* head = impl_->all_nodes.load(std::memory_order_relaxed);
            if (head && head->node)
                head->node->baked_width = width;
        }

        /**
         * @brief Returns the element count stamped on the last node added.
         */
        std::size_t TaskGraph::last_node_width() const noexcept
        {
            NodeEntry* head = impl_->all_nodes.load(std::memory_order_relaxed);
            return head && head->node ? head->node->baked_width : Core::WIDTH_UNKNOWN;
        }

        /**
         * @brief Returns the node most recently added, for a caller reading its state.
         */
        const Node* TaskGraph::last_node_added() const noexcept
        {
            NodeEntry* head = impl_->all_nodes.load(std::memory_order_relaxed);
            return head && head->node ? head->node.get() : nullptr;
        }

        namespace
        {
            /** @brief Narrows @p value to uint32_t, or WIDTH_UNKNOWN when it does not fit. */
            std::uint32_t narrow_width(std::size_t value) noexcept
            {
                constexpr std::size_t max_u32 = static_cast<std::size_t>(WIDTH_UNKNOWN);
                return value <= max_u32 ? static_cast<std::uint32_t>(value) : WIDTH_UNKNOWN;
            }
        } // namespace

        /**
         * @brief Reads @p node's widths as they stand at submit.
         *
         * The baked width stands for both until the bindings hold a live count
         * other than USE_BAKED, which then narrows the live side only. Either
         * width that overflows uint32_t is reported unknown rather than truncated.
         */
        NodeWidths snapshot_widths(const Node& node) noexcept
        {
            NodeWidths widths;
            widths.launched = narrow_width(node.baked_width);
            widths.live = widths.launched;

            if (node.bindings_)
            {
                const std::size_t count = node.bindings_->count.load(std::memory_order_relaxed);
                if (count != LateBindings::USE_BAKED)
                    widths.live = narrow_width(count);
            }
            return widths;
        }

        /**
         * @brief Draws a fresh registration index from this graph's counter.
         *
         * See Impl::registration_counter_; every call returns a distinct value.
         */
        std::uint32_t TaskGraph::reserve_registration() noexcept
        {
            return impl_->registration_counter_.fetch_add(1, std::memory_order_relaxed);
        }

        /**
         * @brief Returns the registration index stamped on the last node added.
         */
        std::uint32_t TaskGraph::last_registration_index() const noexcept
        {
            NodeEntry* head = impl_->all_nodes.load(std::memory_order_relaxed);
            return head && head->node ? head->node->registration_index : NO_REGISTRATION;
        }

        /**
         * @brief Adds a pure-CPU task run natively on a worker thread.
         * @param work Callable run once on a worker thread; submits no SYCL work.
         */
        void TaskGraph::add_native(const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                   const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                   std::function<void()> work,
                                   const std::vector<sycl::event>& dependencies,
                                   std::optional<std::uint32_t> registration)
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

            // The regions stay on the node for dependency identity (src/graph/README.md).
            node->io_reads_  = read_access;
            node->io_writes_ = write_access;

#ifdef SR_DISTRIBUTED
            for (const auto& r : read_access)  node->offload().reads.push_back(r.base);
            for (const auto& w : write_access) node->offload().writes.push_back(w.base);
#endif

            SR_LOG_DEBUG("Native CPU node added (R: {}, W: {}).",
                         read_access.size(), write_access.size());

            impl_->push_node_lock_free(std::move(node), registration);
        }

        /**
         * @brief Compiles the graph from the candidates tagged at insertion (src/graph/README.md).
         * @return The compiled graph ready to run; empty when no node was added.
         * @throws std::runtime_error If the graph has no root node or contains a cycle.
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
                // The tracker is drained before the throw (src/graph/README.md).
                impl_->tracker.reset();
                throw std::runtime_error("Task graph has no root nodes.");
            }

            // A root set does not rule out a disjoint cycle (src/graph/README.md).
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

            // The candidate stacks and history matter only to compile() (src/graph/README.md).
            RawNodeEntry* pr = impl_->potential_roots.exchange(nullptr, std::memory_order_acquire);
            while (pr) { RawNodeEntry* n = pr->next; delete pr; pr = n; }

            RawNodeEntry* pl = impl_->potential_leaves.exchange(nullptr, std::memory_order_acquire);
            while (pl) { RawNodeEntry* n = pl->next; delete pl; pl = n; }

            impl_->tracker.reset();

            // The LIFO drain is reversed to restore insertion order (src/graph/README.md).
            std::reverse(out.begin(), out.end());
            return out;
        }

    } // namespace Graph
} // namespace SushiRuntime