/**************************************************************************/
/* node.hpp                                                               */
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
         * @brief Cross-region (boundary) dependency layer for the dynamic graph.
         *
         * The dynamic region graph splits a node's dependencies into a stable
         * intra-region interior, baked once when the region compiles, and a
         * recomputable cross-region boundary layer the composer maintains as
         * neighbouring regions stream in and out. A static-graph node leaves every
         * field at its default and never reads this layer.
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
         * @brief Per-run profiling accumulators, summed across a run's steps.
         *
         * Written only when the runtime was built with profiling on; otherwise the
         * dispatch and completion paths never touch them. Relaxed atomics: each is a
         * monotonic counter accumulated as steps replay and read once at the run
         * boundary, with no ordering dependency on other state. A given step's
         * completion is reaped by exactly one worker, but different steps may be
         * reaped by different workers, so the adds are atomic.
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
             * @brief Live read/write pointers for a double-buffered offload node.
             *
             * A State node rotates which allocation it reads and writes every step,
             * so the fluent layer re-points these before each step and the
             * coordinator resolves handles from them at dispatch — keeping the
             * compiled plan replayable while following the swap. Null for every
             * ordinary node, whose fixed handles are baked into in_handles/out_handles.
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
         * @brief Optional, lazily-allocated per-node state kept off the node body.
         *
         * Holds concerns absent on most nodes — and, for the distributed offload
         * state, absent entirely in a standalone build. Keeping them behind one
         * pointer fixes Node's layout independent of build flags: SR_DISTRIBUTED
         * changes what a NodeExtensions contains, never sizeof(Node). Empty (and
         * never allocated) in a standalone build.
         *
         * @warning That stabilises Node, not this. THIS type's size still depends
         * on the flag, and the unique_ptr's deleter is instantiated in whichever
         * translation unit destroys the Node — which includes consumer code, since
         * the API layer creates and destroys nodes from header-inline templates.
         * The library and its consumers must therefore still agree on the flag,
         * which is why SR_DISTRIBUTED is a PUBLIC define (see src/CMakeLists.txt).
         * Making it private would require this type to become opaque: declared
         * here, defined and deleted only inside the library.
         */
        struct NodeExtensions
        {
#ifdef SR_DISTRIBUTED
            OffloadState offload;
#endif
        };

        /**
         * @brief One node in the compiled task DAG.
         *
         * Inherits from Task so the scheduler can dispatch it without knowing the DAG
         * layer. Dependency management is lock-free: in_degree is decremented with
         * acq_rel by EventPolling as predecessors complete; when it reaches zero the
         * node is pushed to the ready queue. Successors are stored inline (up to
         * MAX_INLINE_SUCCESSORS) and overflow into a lock-free linked list.
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
             * @brief Cross-region (boundary) dependency layer.
             *
             * The dynamic region graph splits a node's dependencies into a stable
             * intra-region interior and a recomputable cross-region boundary layer;
             * this holds the latter (see RegionLink). Each step resets the live
             * in_degree to region.intra_in_degree + region.boundary_in_degree, so the
             * boundary layer can be re-stitched without disturbing the frozen
             * interior. Left at its defaults by the static graph, which never reads it.
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
             * @brief Marks the graph's completion sentinel.
             *
             * The sentinel delivers run completion (and, on a failed run, the
             * error) through its host_work, so it must run even after a task
             * exception flips the scheduler into drain-without-executing mode —
             * skipping it would leave the completion promise unset and hang
             * execute(). Set by ExecutionEngine when it wires the sentinel.
             */
            bool is_sentinel = false;

            /**
             * @brief USM read/write regions this node touches.
             *
             * Retained on every build (not just distributed) so the scheduler can
             * route a KERNEL submit to the queue whose context actually owns the
             * data — it anchors on a region's base. USM is pinned to the
             * device/context it was allocated on; running the kernel on a sibling
             * device's queue (e.g. CPU USM on a CUDA queue) is an illegal
             * cross-context access. See TaskScheduler::execute_task.
             */
            std::vector<SushiRuntime::Core::ResourceRegion> io_reads_;
            std::vector<SushiRuntime::Core::ResourceRegion> io_writes_;

            /**
             * @brief Set once the node's data has been checked for co-location.
             *
             * The scheduler verifies on a node's first dispatch that all its USM
             * lives on one device context — a single kernel cannot touch two
             * non-peer contexts at once — and caches the result here so later steps
             * pay nothing. A node is dispatched serially across steps, so a plain
             * flag needs no synchronisation. (Cross-device execution is a WP-5
             * concern; until then co-location is the contract this guard enforces.)
             */
            bool locality_checked_ = false;

            /**
             * @brief Cached owning-queue index for this node's anchor USM, or -1.
             *
             * Resolved once on the node's first dispatch (alongside locality_checked_,
             * from the same context_index_for_ptr lookup) when the anchor is a pointer
             * this context allocated. Later steps route straight to
             * RuntimeContext::get_queue(index) instead of get_queue_for_ptr(), which
             * takes a process-global mutex on every device dispatch. -1 keeps the
             * introspection / NUMA round-robin fallback for pointers this context did
             * not allocate (rare). Stable across replay: all_queues never changes and
             * the node's anchor buffer is fixed, so the index cannot go stale.
             * Dispatched serially per node, so a plain int needs no synchronisation.
             */
            int cached_queue_index_ = -1;

            /**
             * @brief Late-bound per-step controls (enabled flag and live size).
             *
             * Null for an ordinary node, so the scheduler's hot path pays one
             * null-pointer load before falling through to normal execution. When
             * set by a dynamic add() overload, the fluent layer re-points its
             * fields each step: the scheduler reads enabled to decide whether to
             * run the node, and the kernel reads count for its iteration range.
             * Bindings may change size/enable freely but never which resources the
             * node touches — that would invalidate the compiled dependency edges
             * and requires a recompile.
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

            /**
             * @brief Identity of the public add() call that registered this node.
             *
             * Stamped once by TaskGraph at insertion time from a per-graph counter,
             * in call order. Unlike plan_index, which reflects the compiled plan's
             * position, this reflects registration order. Every internal node one
             * public add() call creates (e.g. a reduction's tile levels) shares this
             * value. Zero until the node is registered.
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
             * @brief Registers @p target as a successor of this node.
             *
             * Fills the inline buffer first; once it is full, pushes @p target onto
             * the lock-free successor_list via compare_exchange_weak with
             * release/relaxed ordering — release so the stored pointer is visible to
             * any thread that reads successor_list with acquire ordering.
             *
             * @param target Node to enqueue when this node completes.
             */
            void add_successor(Node* target) noexcept
            {
                // Single-writer per node: successors are wired at graph build /
                // step-boundary time, never concurrently with another add_successor
                // on the same node. That lets us PUBLISH THE SLOT BEFORE THE COUNT:
                // write inline_successors[idx], then release-store the incremented
                // count. A concurrent reader in trigger_successors acquire-loads the
                // count and, on seeing idx+1, is guaranteed to also see the slot
                // write — closing the window where it read an incremented count but a
                // still-null slot and silently skipped the successor (a missed
                // in_degree decrement -> deadlock). The previous relaxed fetch_add
                // published the count first, exposing that window.
                int32_t idx = inline_successor_count.load(std::memory_order_relaxed);
                if (idx < MAX_INLINE_SUCCESSORS)
                {
                    inline_successors[idx] = target;
                    inline_successor_count.store(idx + 1, std::memory_order_release);
                    return;
                }
                // Past the inline capacity: still reserve the slot number so the
                // count reflects the spilled entries, then push onto the list below.
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
             *
             * Sets in_degree to region.intra_in_degree + region.boundary_in_degree,
             * the count the scheduler decrements as predecessors complete. The
             * dynamic graph calls this for every node before each replayed step, the
             * equivalent of CompiledGraph::rearm for a plan whose boundary layer is
             * maintained incrementally rather than snapshotted.
             */
            void arm_in_degree() noexcept
            {
                in_degree.store(region.intra_in_degree
                                + region.boundary_in_degree.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
            }

#ifdef SR_DISTRIBUTED
            /**
             * @brief Returns the offload state, allocating the extensions on demand.
             *
             * The mutable entry point for the distributed build paths that populate
             * or update offload metadata. The first call allocates this node's
             * NodeExtensions; later calls reuse it.
             *
             * @return Reference to this node's offload state.
             */
            OffloadState& offload()
            {
                if (!ext_)
                    ext_ = std::make_unique<NodeExtensions>();
                return ext_->offload;
            }

            /**
             * @brief Returns the offload state if present, else nullptr.
             *
             * A node that never offloads never allocates its extensions, so the read
             * paths (the scheduler's offloadable check, the completion observer) read
             * a null return as "not offloadable / nothing to bump" without forcing an
             * allocation.
             *
             * @return Pointer to the offload state, or nullptr if this node has none.
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
             *
             * Walks the three places a successor can live — the inline array, the
             * lock-free spillover list, and the cross-region boundary vector — and
             * decrements each target's in-degree. A target whose count reaches zero
             * has no dependencies left and goes to the sink. The decrement is
             * acq_rel so the freed successor sees every write the predecessors made.
             *
             * @param sink Collects each successor this completion made runnable.
             */
            void on_complete(SushiRuntime::Scheduler::ReadySink& sink) override
            {
                const std::int32_t count =
                    inline_successor_count.load(std::memory_order_acquire);
                const std::int32_t max_idx = std::min(count, MAX_INLINE_SUCCESSORS);

                for (std::int32_t i = 0; i < max_idx; ++i)
                {
                    Node* succ = inline_successors[i];
                    if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                        sink.push_back(succ);
                }

                for (SuccessorNode* curr = successor_list.load(std::memory_order_acquire);
                     curr; curr = curr->next)
                {
                    Node* succ = curr->target;
                    if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                        sink.push_back(succ);
                }

                // Cross-region successors of the dynamic region graph. This vector is
                // mutated only at a step boundary, with no worker running, and is
                // read-only here, so it needs no atomics of its own; the per-target
                // in_degree decrement carries the same acq_rel ordering as above.
                // Empty (and so skipped) for every static-graph node and every dynamic
                // node that exports nothing across a boundary.
                for (Node* succ : region.boundary_successors)
                {
                    if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                        sink.push_back(succ);
                }
            }

            /**
             * @brief Adds one completion to this node's profiling counters.
             *
             * The sentinel is skipped: it is plumbing that closes the step, not a
             * system the caller asked to run, so counting it would put a row in the
             * report for work nobody wrote. Relaxed ordering throughout — the numbers
             * are read after the step's completion barrier, which supplies the
             * happens-before on its own.
             *
             * @param device_ns Kernel device time for this completion; 0 when the
             *                  backend reported none, in which case only the
             *                  invocation count moves.
             */
            void record_completion(std::uint64_t device_ns) noexcept override
            {
                if (is_sentinel)
                    return;
                profiling.invocations.fetch_add(1, std::memory_order_relaxed);
                if (device_ns)
                    profiling.device_ns.fetch_add(device_ns, std::memory_order_relaxed);
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
