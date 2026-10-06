/**************************************************************************/
/* node.hpp                                                               */
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
 * @file node.hpp
 * @brief Declares Node, one node of the compiled task DAG, and its supporting types.
 * @author Mustafa Garip
 * @see include/SushiRuntime/graph/README.md
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>
#include <functional>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/dispatch_width.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/core/resource_region.hpp>
#include <SushiRuntime/graph/late_bindings.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#ifdef SR_DISTRIBUTED
#include <SushiRuntime/distributed/protocol.hpp>
#endif

namespace SushiRuntime 
{
    namespace Graph 
    {
        /** @brief Work definition for a task node. */
        using TaskWork = std::function<void(sycl::handler&)>;
        
        /** @brief Work definition for a host-side library call node. */
        using HostWork = std::function<sycl::event(sycl::queue&, const std::vector<sycl::event>&)>;

        /**
         * @brief Inline-optimised storage for a node's SYCL event dependencies.
         *
         * Holds up to STATIC_CAP events without a heap allocation; additional
         * events spill into a dynamic_data vector. Avoids per-node vector
         * allocation in the common case (≤ 8 deps).
         */
        struct SmallEventVector
        {
            static constexpr size_t STATIC_CAP = 8;

            size_t static_count = 0;                /**< Number of events in the inline buffer. */
            sycl::event static_data[STATIC_CAP];   /**< Inline event storage. */
            std::vector<sycl::event> dynamic_data; /**< Overflow storage when count > STATIC_CAP. */

            /**
             * @brief Replaces the stored events with those from @p vec.
             * @param vec Source event list.
             * @return *this.
             */
            inline SmallEventVector& operator=(const std::vector<sycl::event>& vec)
            {
                static_count = 0;
                dynamic_data.clear();

                for (const auto& e : vec) 
                {
                    if (static_count < STATIC_CAP) 
                        static_data[static_count++] = e;
                    else 
                        dynamic_data.push_back(e);
                }
                return *this;
            }

            /**
             * @brief Returns all stored events as a single vector.
             * @return Vector containing inline events followed by any overflow events.
             */
            inline std::vector<sycl::event> to_vector() const
            {
                std::vector<sycl::event> res;
                res.reserve(static_count + dynamic_data.size());

                for (size_t i = 0; i < static_count; ++i)
                    res.push_back(static_data[i]);

                for (const auto& e : dynamic_data)
                    res.push_back(e);

                return res;
            }

            /** @brief Read-only forward iterator over the inline buffer then the overflow vector. */
            struct const_iterator
            {
                const SmallEventVector* owner; /**< Vector being iterated. */
                size_t index;                  /**< Position in the combined inline-then-overflow sequence. */

                /**
                 * @brief Dereferences the event at the current position.
                 * @return The event, from inline storage or the overflow vector depending on @p index.
                 */
                inline const sycl::event& operator*() const
                {
                    return index < owner->static_count
                        ? owner->static_data[index]
                        : owner->dynamic_data[index - owner->static_count];
                }

                inline const_iterator& operator++() { ++index; return *this; }
                inline bool operator!=(const const_iterator& other) const { return index != other.index; }
                inline bool operator==(const const_iterator& other) const { return index == other.index; }
            };

            /**
             * @brief Number of events currently stored, inline plus overflow.
             * @return The event count.
             */
            inline size_t size() const { return static_count + dynamic_data.size(); }

            /** @brief Iterator to the first stored event, or end() when empty. */
            inline const_iterator begin() const { return {this, 0}; }

            /** @brief Iterator past the last stored event. */
            inline const_iterator end() const { return {this, static_count + dynamic_data.size()}; }
        };

        struct Node;

        /**
         * @brief Node in the lock-free spillover successor list.
         *
         * Used when a node's successor count exceeds MAX_INLINE_SUCCESSORS; additional
         * successors are chained via a compare-exchange push-front into Node::successor_list.
         */
        struct SuccessorNode
        {
            Node* target;           /**< Successor graph node to release when this node completes. */
            SuccessorNode* next;    /**< Next element in the linked list; null for the list tail. */
        };

        /**
         * @brief Holds the cross-region (boundary) dependency layer for the dynamic graph.
         * @see include/SushiRuntime/graph/README.md
         */
        struct RegionLink
        {
            /**
             * @brief Predecessor count from edges internal to the owning region.
             *
             * Captured once when the region is compiled — it equals the live
             * in_degree right after the region's own dependency pass — and never
             * touched again while the region is unchanged.
             */
            int32_t intra_in_degree = 0;

            /**
             * @brief Predecessor count from cross-region (boundary) edges.
             *
             * Maintained incrementally by the composer as boundary edges are added
             * and removed; summed with intra_in_degree to re-arm the node each step.
             */
            std::atomic<int32_t> boundary_in_degree{0};

            /**
             * @brief Cross-region successors released in addition to the intra ones.
             *
             * The scheduler's trigger_successors walks this list alongside the
             * inline/spillover intra successors, decrementing each target's
             * in_degree. Mutated only at a step boundary (no worker running) and
             * read-only during a step, so a plain vector needs no synchronisation.
             */
            std::vector<Node*> boundary_successors;

            /**
             * @brief True when this node has no successor internal to its region.
             *
             * An intra-leaf is a candidate to link to the completion sentinel, but
             * only while it also has no cross-region successor. Set once when the
             * region is compiled.
             */
            bool intra_leaf = false;

            /**
             * @brief Whether this node currently releases the completion sentinel.
             *
             * The composer keeps the sentinel wired to exactly the current leaves
             * (no intra and no cross-region successor). Flipped as boundary edges
             * make a leaf gain or lose an exported successor.
             */
            bool sentinel_linked = false;

            /**
             * @brief Clears only the cross-region boundary layer.
             *
             * Drops the boundary predecessor count and the boundary successor list,
             * leaving the frozen intra-region edges untouched. The composer calls
             * this on a pin before re-deriving the boundary edges of a base that
             * became dirty. Safe at a step boundary only.
             */
            void clear() noexcept
            {
                boundary_in_degree.store(0, std::memory_order_relaxed);
                boundary_successors.clear();
            }
        };

        /**
         * @brief Holds per-run profiling accumulators, summed across a run's steps.
         * @see include/SushiRuntime/graph/README.md
         */
        struct NodeProfiling
        {
            std::atomic<std::uint64_t> invocations{0};  /**< Dispatch count over the run. */
            std::atomic<std::uint64_t> device_ns{0};    /**< Summed kernel device time (ns). */
            std::atomic<std::uint64_t> host_ns{0};      /**< Summed native/host wall time (ns). */
        };

#ifdef SR_DISTRIBUTED
        /**
         * @brief Distributed offload state, stamped by TaskGraph::compile().
         *
         * Only meaningful in Master mode. offloadable stays false for the vast
         * majority of (small, local) tasks, so the scheduler's hot path pays just a
         * single bool load before falling through to local execution.
         */
        struct OffloadState
        {
            /** @brief Whether this node may ship to a worker (else it runs locally). */
            bool offloadable = false;
            /** @brief Remote kernel id used when offloading (valid iff offloadable). */
            OpID remote_op;
            /** @brief Serializable handles for the buffers this node reads / writes. */
            std::vector<SushiRuntime::Distributed::BufferHandle> in_handles;
            std::vector<SushiRuntime::Distributed::BufferHandle> out_handles;
            /** @brief Raw read/write USM pointers retained so compile() can resolve handles. */
            std::vector<void*> reads;
            std::vector<void*> writes;

            /**
             * @brief Hold the live read and write pointers of a double-buffered offload node.
             * @see include/SushiRuntime/graph/README.md
             */
            std::shared_ptr<std::vector<void*>> live_reads;
            std::shared_ptr<std::vector<void*>> live_writes;

            /**
             * @brief How many times this node has been (re)dispatched to a worker.
             *
             * After a worker death or a failed remote result the node is
             * re-dispatched; once attempts exceed a cap the coordinator pins it
             * local (offloadable cleared) so a kernel that consistently fails
             * remotely cannot loop forever.
             */
            std::atomic<uint32_t> attempts{0};
        };
#endif

        /**
         * @brief Holds optional, lazily allocated per-node state kept off the node body.
         * @warning The library and its consumers must agree on SR_DISTRIBUTED; this type's size
         *          depends on it.
         * @see include/SushiRuntime/graph/README.md
         */
        struct NodeExtensions
        {
#ifdef SR_DISTRIBUTED
            OffloadState offload;
#endif
        };

        /**
         * @brief Represents one node in the compiled task DAG.
         * @see include/SushiRuntime/graph/README.md
         */
        struct Node : public SushiRuntime::Scheduler::Task
        {
            /** @brief The semantic metadata defining this task. */
            TaskMetadata metadata;

            /** @brief The work to be executed in this node. */
            TaskWork work;
            
            /** @brief The host work to be executed in this node if it's a HOST task. */
            HostWork host_work;

            /** @brief Pure-CPU work run natively on the worker thread (no SYCL queue). */
            std::function<void()> native_work;

            /** @brief The execution mode of this node. */
            ExecutionMode mode = ExecutionMode::KERNEL;

            /**
             * @brief Whether this node submits SYCL device work (the safe default).
             *
             * Cleared by the native host path so the scheduler runs native_work
             * directly instead of routing through a queue. A node that submits a
             * kernel from its host_work must keep this true.
             */
            bool submits_device_work_ = true;

            /** @brief Number of incoming dependencies. */
            std::atomic<int32_t> in_degree{0};

            /**
             * @brief Holds this node's cross-region (boundary) dependency layer; see RegionLink.
             * @see include/SushiRuntime/graph/README.md
             */
            RegionLink region;

            /** @brief Head of the lock-free successor list (for spillover). */
            std::atomic<SuccessorNode*> successor_list{nullptr};

            /** @brief Inline buffer for successors to avoid heap allocations. */
            static constexpr int MAX_INLINE_SUCCESSORS = 16;
            std::atomic<int32_t> inline_successor_count{0};
            Node* inline_successors[MAX_INLINE_SUCCESSORS] = {nullptr};
            
            /** @brief SYCL events this node depends on (heap-free for small amounts). */
            SmallEventVector sycl_deps;

            /**
             * @brief Marks the graph's completion sentinel, which must run even after a task
             *        exception.
             * @see include/SushiRuntime/graph/README.md
             */
            bool is_sentinel = false;

            /**
             * @brief Hold the USM read and write regions this node touches.
             * @see include/SushiRuntime/graph/README.md
             */
            std::vector<SushiRuntime::Core::ResourceRegion> io_reads_;
            std::vector<SushiRuntime::Core::ResourceRegion> io_writes_;

            /**
             * @brief Tells whether the node's data has been checked for co-location.
             * @see include/SushiRuntime/graph/README.md
             */
            bool locality_checked_ = false;

            /**
             * @brief Caches the owning-queue index of this node's anchor USM, or -1.
             * @see include/SushiRuntime/graph/README.md
             */
            int cached_queue_index_ = -1;

            /**
             * @brief Holds the late-bound per-step controls; null for an ordinary node.
             * @see include/SushiRuntime/graph/README.md
             */
            std::shared_ptr<LateBindings> bindings_;

            /**
             * @brief Stable identity of this node in the compiled plan.
             *
             * Assigned once by CompiledGraph::capture_base_state() as the node's
             * position in all_nodes, so a RunReport row can name a node that has no
             * label and two same-named nodes stay distinct. Zero until compiled.
             */
            std::size_t plan_index = 0;

            /** @brief Elements this node's dispatch launches over, or Core::WIDTH_UNKNOWN. */
            std::size_t baked_width = Core::WIDTH_UNKNOWN;

            /**
             * @brief Identifies the public add() call that registered this node; zero until
             *        registered.
             * @see include/SushiRuntime/graph/README.md
             */
            std::uint32_t registration_index = 0;

            /** @brief Per-run profiling accumulators (see NodeProfiling). */
            NodeProfiling profiling;

            /**
             * @brief Lazily-allocated optional state (see NodeExtensions).
             *
             * Null on most nodes and on every node in a standalone build. Present
             * unconditionally so Node's layout never depends on a build flag; the
             * distributed offload state lives here, reached through offload() /
             * offload_state().
             */
            std::unique_ptr<NodeExtensions> ext_;

            /**
             * @brief Registers target as a successor of this node.
             * @see include/SushiRuntime/graph/README.md
             */
            void add_successor(Node* target) noexcept
            {
                // Single writer per node: the slot is written before the count is published.
                int32_t idx = inline_successor_count.load(std::memory_order_relaxed);
                if (idx < MAX_INLINE_SUCCESSORS)
                {
                    inline_successors[idx] = target;
                    inline_successor_count.store(idx + 1, std::memory_order_release);
                    return;
                }
                // Past the inline capacity the count still advances to include spilled entries.
                inline_successor_count.store(idx + 1, std::memory_order_release);

                // Spillover to heap-allocated lock-free linked list
                SuccessorNode* new_node = new SuccessorNode{target, nullptr};
                SuccessorNode* old_head = successor_list.load(std::memory_order_relaxed);
                do 
                {
                    new_node->next = old_head;
                } 
                while (!successor_list.compare_exchange_weak(old_head, new_node, 
                                                              std::memory_order_release, 
                                                              std::memory_order_relaxed));
            }

            /**
             * @brief Re-arms the live in-degree from the two-layer baseline.
             * @see include/SushiRuntime/graph/README.md
             */
            void arm_in_degree() noexcept
            {
                in_degree.store(region.intra_in_degree
                                + region.boundary_in_degree.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
            }

#ifdef SR_DISTRIBUTED
            /**
             * @brief Returns the offload state, allocating the extensions on first use.
             * @see include/SushiRuntime/graph/README.md
             */
            OffloadState& offload()
            {
                if (!ext_)
                    ext_ = std::make_unique<NodeExtensions>();
                return ext_->offload;
            }

            /**
             * @brief Returns the offload state if present, else nullptr, without allocating.
             * @see include/SushiRuntime/graph/README.md
             */
            OffloadState* offload_state() noexcept
            {
                return ext_ ? &ext_->offload : nullptr;
            }

            /** @brief Const overload of offload_state(). */
            const OffloadState* offload_state() const noexcept
            {
                return ext_ ? &ext_->offload : nullptr;
            }
#endif

            /** @brief Returns the execution mode for this node. */
            ExecutionMode get_execution_mode() const override   { return mode; }

            /** @brief Indicates that this task is a graph node. */
            bool is_graph_node() const noexcept override        { return true; }

            /**
             * @brief Satisfies one dependency on every successor and reports the freed ones.
             * @param sink Collects each successor this completion made runnable.
             * @see include/SushiRuntime/graph/README.md
             */
            void on_complete(SushiRuntime::Scheduler::ReadySink& sink) override
            {
                for_each_successor([&sink](Node* succ)
                {
                    if (succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                        sink.push_back(succ);
                });
            }

            /**
             * @brief Calls @p visit once for every non-null successor, in storage order.
             *
             * Walks the inline array, the spillover list, then the cross-region
             * boundary vector, which is mutated only at a step boundary and so
             * needs no atomics of its own.
             * @param visit Callable invoked as visit(Node*).
             */
            template <class Visit>
            void for_each_successor(Visit&& visit) const
            {
                const std::int32_t count =
                    inline_successor_count.load(std::memory_order_acquire);
                const std::int32_t max_idx = std::min(count, MAX_INLINE_SUCCESSORS);

                for (std::int32_t i = 0; i < max_idx; ++i)
                    if (Node* succ = inline_successors[i])
                        visit(succ);

                for (SuccessorNode* curr = successor_list.load(std::memory_order_acquire);
                     curr; curr = curr->next)
                    if (Node* succ = curr->target)
                        visit(succ);

                for (Node* succ : region.boundary_successors)
                    if (succ)
                        visit(succ);
            }

            /** @brief Returns prioritized value from metadata. */
            int get_priority() const noexcept override          { return metadata.priority; }

            /** @brief Reports whether this node submits SYCL device work. */
            bool submits_device_work() const noexcept override  { return submits_device_work_; }

            /** @brief Runs the node's pure-CPU work directly on the worker thread. */
            void execute_native() override                      { if(native_work) native_work(); }

            /** @brief Executes the task work within a SYCL handler. */
            void execute(sycl::handler& h) override             { if(work) work(h); }

            /** @brief Executes the host work with a queue. */
            sycl::event execute_host(sycl::queue& q, const std::vector<sycl::event>& deps) override 
            {
                if(host_work) return host_work(q, deps);
                return {};
            }

            /** @brief Optionally splits the task into smaller subtasks for better load balancing. */
            std::optional<sushi_ptr<SushiRuntime::Scheduler::Task>> split() override { return std::nullopt; }

            /** @brief Returns true if the task can be split. */
            bool is_splittable() const override { return false; }

            /** @brief Destructor cleans up the atomic successor list. */
            ~Node() 
            {
                SuccessorNode* curr = successor_list.load(std::memory_order_relaxed);
                while(curr) 
                {
                    SuccessorNode* next = curr->next;
                    delete curr;
                    curr = next;
                }
            }
        };

        /** @brief Pairs the range a dispatch launched over with the count that did work. */
        struct NodeWidths
        {
            std::uint32_t launched = Core::WIDTH_UNKNOWN; /**< Elements the dispatch launched over. */
            std::uint32_t live     = Core::WIDTH_UNKNOWN; /**< Elements that did work. */
        };

        /**
         * @brief Reads @p node's widths as they stand at submit.
         * @param node A compiled node, with or without late bindings.
         * @return The baked width for both, narrowed by the bindings' live count when it holds one.
         */
        SUSHIRUNTIME_API NodeWidths snapshot_widths(const Node& node) noexcept;

    } // namespace Graph
} // namespace SushiRuntime
