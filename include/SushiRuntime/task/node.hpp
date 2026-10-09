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
 * @brief Declares Node, one node of the compiled task DAG, and its successor list entry.
 * @author Mustafa Garip
 * @see include/SushiRuntime/graph/README.md
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/dispatch_width.hpp>
#include <SushiRuntime/core/resource_region.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node_profiling.hpp>
#include <SushiRuntime/task/ready_sink.hpp>
#include <SushiRuntime/task/region_link.hpp>
#include <SushiRuntime/task/small_event_vector.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>
#ifdef SR_DISTRIBUTED
#include <SushiRuntime/distributed/protocol.hpp>
#endif

namespace SushiRuntime
{
    namespace Graph
    {
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

            /** @brief Returns metadata.name, the node's only label. */
            const char* get_name() const noexcept override      { return metadata.name; }

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

    } // namespace Graph
} // namespace SushiRuntime
