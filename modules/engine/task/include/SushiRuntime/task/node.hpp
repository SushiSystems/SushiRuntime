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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file node.hpp
 * @brief Declares Node, one node of the compiled task DAG, and its successor list entry.
 * @author Mustafa Garip
 * @see modules/engine/graph/README.md
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/machine/node_split.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node_profiling.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/placed_split.hpp>
#include <SushiRuntime/task/ready_sink.hpp>
#include <SushiRuntime/task/region_link.hpp>
#include <SushiRuntime/task/small_event_vector.hpp>
#include <SushiRuntime/task/submit_form.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct Node;

        /**
         * @brief Node in the lock-free spillover successor list.
         *
         * Used when a node's successor count exceeds K_MAX_INLINE_SUCCESSORS; additional
         * successors are chained via a compare-exchange push-front into Node::successor_list.
         */
        struct SuccessorNode
        {
            Node* target;           /**< Successor graph node to release when this node completes. */
            SuccessorNode* next;    /**< Next element in the linked list; null for the list tail. */
        };

        /**
         * @brief Represents one node in the compiled task DAG.
         * @see modules/engine/graph/README.md
         */
        struct Node : public SushiRuntime::Internal::Scheduler::Task
        {
            /** @brief The semantic metadata defining this task. */
            TaskMetadata metadata;

            /** @brief The work to be executed in this node. */
            TaskWork work;
            
            /** @brief The host work to be executed in this node if it's a HOST task. */
            HostWork host_work;

            /**
             * @brief The host work that reports its failure as a status; it runs before host_work
             *        when set.
             */
            CheckedHostWork checked_host_work;

            /** @brief Pure-CPU work run natively on the worker thread (no SYCL queue). */
            std::function<void()> native_work;

            /**
             * @brief The launch run as a host loop, for a runtime with no device; empty when the
             *        node has no host form.
             * @see modules/engine/task/README.md
             */
            std::function<void()> host_form;

            /** @brief The execution mode of this node. */
            ExecutionMode mode = ExecutionMode::Kernel;

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
             * @see modules/engine/graph/README.md
             */
            RegionLink region;

            /** @brief Head of the lock-free successor list (for spillover). */
            std::atomic<SuccessorNode*> successor_list{nullptr};

            /** @brief Inline buffer for successors to avoid heap allocations. */
            static constexpr int K_MAX_INLINE_SUCCESSORS = 16;
            std::atomic<int32_t> inline_successor_count{0};
            Node* inline_successors[K_MAX_INLINE_SUCCESSORS] = {nullptr};
            
            /** @brief SYCL events this node depends on (heap-free for small amounts). */
            SmallEventVector sycl_deps;

            /**
             * @brief Marks the graph's completion sentinel, which must run even after a task
             *        exception.
             * @see modules/engine/graph/README.md
             */
            bool is_sentinel = false;

            /**
             * @brief Hold the USM read and write regions this node touches.
             * @see modules/engine/graph/README.md
             */
            std::vector<SushiRuntime::BufferRegion> io_reads_;
            std::vector<SushiRuntime::BufferRegion> io_writes_;

            /**
             * @brief Tells whether the node's data has been checked for co-location.
             * @see modules/engine/graph/README.md
             */
            bool locality_checked_ = false;

            /** @brief Stands for "no device stated" in placement_. */
            static constexpr std::uint16_t K_NO_PLACEMENT = 0xFFFF;

            /**
             * @brief Holds the device place_on() stated, or K_NO_PLACEMENT.
             * @see modules/engine/graph/README.md
             */
            std::uint16_t placement_ = K_NO_PLACEMENT;

            /**
             * @brief Caches the index of the device queue this node dispatches to; -1 until
             *        the first dispatch.
             * @see modules/engine/graph/README.md
             */
            int cached_queue_index_ = -1;

            /**
             * @brief Holds the late-bound per-step controls; null for an ordinary node.
             * @see modules/engine/graph/README.md
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

            /** @brief Elements this node launches over, or K_WIDTH_UNKNOWN. */
            std::size_t baked_width = K_WIDTH_UNKNOWN;

            /** @brief How the recorder said this node may be cut into parts; null for a node that states none. */
            std::shared_ptr<const NodeSplit> declared_split;

            /**
             * @brief The parts the partitioner cut this node into; null for a node that runs whole.
             * @see modules/engine/task/README.md
             */
            std::shared_ptr<const PlacedSplit> placed_split;

            /**
             * @brief Says what a recorded command graph may keep of this node's submission.
             * @see docs/design/GRAPH_REPLAY/README.md
             */
            SubmitForm submit_form = SubmitForm::StepBound;

            /**
             * @brief Identifies the public add() call that registered this node; zero until
             *        registered.
             * @see modules/engine/graph/README.md
             */
            std::uint32_t registration_index = 0;

            /** @brief Per-run profiling accumulators (see NodeProfiling). */
            NodeProfiling profiling;

            /**
             * @brief Holds what an offload coordinator keeps on this node; null on most nodes.
             * @see modules/engine/task/README.md
             */
            std::unique_ptr<OffloadState> offload_state_;

            /**
             * @brief Holds the pointers the kernel receives, rebound to the node's device at each
             *        dispatch; null on a node whose kernel takes no captured pointer.
             * @see modules/engine/task/README.md
             */
            std::unique_ptr<KernelPointers> kernel_pointers_;

            /**
             * @brief Says whether the kernel can run on a device other than the home of its
             *        data; set where the node is built, never inferred from kernel_pointers_.
             * @see modules/engine/task/README.md
             */
            KernelAddressing kernel_addressing = KernelAddressing::FixedToHome;

            /**
             * @brief States the device a node that names no owned region runs on.
             * @param device Index into the runtime's device set; must be below K_NO_PLACEMENT.
             * @pre The node has not been dispatched yet.
             * @see modules/engine/graph/README.md
             */
            void place_on(std::size_t device)
            {
                SR_VERIFY(device < K_NO_PLACEMENT,
                          ("Node::place_on(" + std::to_string(device) +
                           "): a device index must be below " +
                           std::to_string(K_NO_PLACEMENT) +
                           "; pass the index of a device of the runtime.")
                              .c_str());

                placement_ = static_cast<std::uint16_t>(device);
            }

            /** @brief Returns the pointers the kernel receives, or null for a kernel that takes none. */
            [[nodiscard]] KernelPointers* kernel_pointers() noexcept
            {
                return kernel_pointers_.get();
            }

            /** @brief Returns the device place_on() stated, or nothing when none was stated. */
            std::optional<std::size_t> stated_placement() const noexcept
            {
                if (placement_ == K_NO_PLACEMENT)
                    return std::nullopt;
                return static_cast<std::size_t>(placement_);
            }

            /**
             * @brief Registers target as a successor of this node.
             * @pre Only one thread calls add_successor() on this node at a time.
             * @throws std::bad_alloc When a spillover entry cannot be allocated. The count has
             *         already advanced, and target is not linked.
             * @see modules/engine/graph/README.md
             */
            void add_successor(Node* target)
            {
                // The slot is written before the count is published.
                int32_t idx = inline_successor_count.load(std::memory_order_relaxed);
                if (idx < K_MAX_INLINE_SUCCESSORS)
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
             * @see modules/engine/graph/README.md
             */
            void arm_in_degree() noexcept
            {
                in_degree.store(region.intra_in_degree
                                + region.boundary_in_degree.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
            }

            /**
             * @brief Returns the offload state, allocating it on first use.
             * @see modules/engine/task/README.md
             */
            OffloadState& offload()
            {
                if (!offload_state_)
                    offload_state_ = std::make_unique<OffloadState>();
                return *offload_state_;
            }

            /** @brief Returns the offload state if present, else nullptr, without allocating. */
            OffloadState* offload_state() noexcept
            {
                return offload_state_.get();
            }

            /** @brief Const overload of offload_state(). */
            const OffloadState* offload_state() const noexcept
            {
                return offload_state_.get();
            }

            /**
             * @brief Lists the handles this node reads and writes at its next dispatch.
             *
             * For a node whose buffers rotate every step; whoever owns the lists re-points
             * them before each step.
             * @see modules/engine/task/README.md
             */
            void set_live_io(std::shared_ptr<std::vector<GlobalHandle>> reads,
                             std::shared_ptr<std::vector<GlobalHandle>> writes)
            {
                OffloadState& state = offload();
                state.live_reads = std::move(reads);
                state.live_writes = std::move(writes);
            }

            /** @brief Returns metadata.name, the node's only label. */
            const char* name() const noexcept override      { return metadata.name; }

            /** @brief Returns the execution mode for this node. */
            ExecutionMode execution_mode() const override   { return mode; }

            /** @brief Indicates that this task is a graph node. */
            bool is_graph_node() const noexcept override        { return true; }

            /**
             * @brief Satisfies one dependency on every successor and reports the freed ones.
             * @param sink Collects each successor this completion made runnable.
             * @see modules/engine/graph/README.md
             */
            void on_complete(SushiRuntime::Internal::Scheduler::ReadySink& sink) override
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
                const std::int32_t max_idx = std::min(count, K_MAX_INLINE_SUCCESSORS);

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
            int priority() const noexcept override          { return metadata.priority; }

            /** @brief Reports whether this node submits SYCL device work. */
            bool submits_device_work() const noexcept override  { return submits_device_work_; }

            /** @brief Runs the node's pure-CPU work directly on the worker thread. */
            void execute_native() override                      { if(native_work) native_work(); }

            /**
             * @brief Runs the node on the calling thread, with no device: a native host node runs
             *        its work, a launch runs its host form.
             * @return What the work threw, as an Error, or an Unsupported error for a node with no
             *         host form: an NdRange, a raw command group or a queue body.
             * @see modules/engine/task/README.md
             */
            [[nodiscard]] Result<void> run_on_host();

            /**
             * @brief Executes the task work within a SYCL handler, with the kernel's pointers bound
             *        to the node's device when it has any.
             * @see modules/engine/task/README.md
             */
            void execute(sycl::handler& h) override
            {
                if (!work)
                    return;
                if (!kernel_pointers_ || cached_queue_index_ < 0)
                {
                    work(h);
                    return;
                }
                kernel_pointers_->bind(static_cast<std::size_t>(cached_queue_index_));
                const KernelPointerScope scope(*kernel_pointers_);
                work(h);
            }

            /**
             * @brief Executes the queue body, with its operands' pointers bound to the node's
             *        device when it has any.
             * @see modules/engine/task/README.md
             */
            sycl::event execute_host(sycl::queue& queue,
                                     const std::vector<sycl::event>& dependencies) override
            {
                if (!host_work)
                    return {};
                if (!kernel_pointers_ || cached_queue_index_ < 0)
                    return host_work(queue, dependencies);
                kernel_pointers_->bind(static_cast<std::size_t>(cached_queue_index_));
                const KernelPointerScope scope(*kernel_pointers_);
                return host_work(queue, dependencies);
            }

            /**
             * @brief Executes the checked host work when the node has it, else the queue body,
             *        with the operands' pointers bound as execute_host() binds them.
             * @return The event, or the error the checked work returned.
             * @see modules/engine/task/README.md
             */
            Result<sycl::event> execute_host_checked(sycl::queue& queue,
                                                     const std::vector<sycl::event>& dependencies) override
            {
                if (!checked_host_work)
                    return execute_host(queue, dependencies);
                if (!kernel_pointers_ || cached_queue_index_ < 0)
                    return checked_host_work(queue, dependencies);
                kernel_pointers_->bind(static_cast<std::size_t>(cached_queue_index_));
                const KernelPointerScope scope(*kernel_pointers_);
                return checked_host_work(queue, dependencies);
            }

            /** @brief Optionally splits the task into smaller subtasks for better load balancing. */
            std::optional<sushi_ptr<SushiRuntime::Internal::Scheduler::Task>> split() override { return std::nullopt; }

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
