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
            
            /** @brief Head of the lock-free successor list (for spillover). */
            std::atomic<SuccessorNode*> successor_list{nullptr};

            /** @brief Inline buffer for successors to avoid heap allocations. */
            static constexpr int MAX_INLINE_SUCCESSORS = 16;
            std::atomic<int32_t> inline_successor_count{0};
            Node* inline_successors[MAX_INLINE_SUCCESSORS] = {nullptr};
            
            /** @brief SYCL events this node depends on (heap-free for small amounts). */
            SmallEventVector sycl_deps;
            
            /** @brief Flag to indicate if the node is fused with others. */
            bool is_fused = false;

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
             * @brief Profiling accumulators, summed across a run's steps.
             *
             * Written only when the runtime was built with profiling on; otherwise
             * the dispatch and completion paths never touch them. Relaxed atomics:
             * each is a monotonic counter accumulated as steps replay and read once
             * at the run boundary, with no ordering dependency on other state. A
             * given step's completion is reaped by exactly one worker, but different
             * steps may be reaped by different workers, so the adds are atomic.
             */
            std::atomic<std::uint64_t> prof_invocations{0};  /**< Dispatch count over the run. */
            std::atomic<std::uint64_t> prof_device_ns{0};    /**< Summed kernel device time (ns). */
            std::atomic<std::uint64_t> prof_host_ns{0};      /**< Summed native/host wall time (ns). */

#ifdef SR_DISTRIBUTED
            /**
             * @brief Distributed offload metadata, stamped by TaskGraph::compile().
             *
             * Only meaningful in Master mode. offloadable_ stays false for the
             * vast majority of (small, local) tasks, so the scheduler's hot path
             * pays just a single bool load before falling through to local exec.
             */
            bool offloadable_ = false;
            /** @brief Remote kernel id used when offloading (valid iff offloadable_). */
            OpID remote_op_;
            /** @brief Serializable handles for the buffers this node reads / writes. */
            std::vector<SushiRuntime::Distributed::BufferHandle> in_handles_;
            std::vector<SushiRuntime::Distributed::BufferHandle> out_handles_;
            /** @brief Raw read/write USM pointers retained so compile() can resolve handles. */
            std::vector<void*> dist_reads_;
            std::vector<void*> dist_writes_;

            /**
             * @brief Live read/write pointers for a double-buffered offload node.
             *
             * Ordinary offload nodes touch fixed allocations, so compile() bakes
             * their handles once into in_handles_/out_handles_. A double-buffered
             * (State) node instead rotates which allocation it reads and writes every
             * step, so baking would address the wrong buffer on alternating steps.
             * When these are set, the fluent layer re-points them at the step's
             * current/next allocations before each step and the coordinator resolves
             * handles from them at dispatch — keeping the compiled plan replayable
             * while following the swap. Null for every ordinary node.
             */
            std::shared_ptr<std::vector<void*>> live_reads_;
            std::shared_ptr<std::vector<void*>> live_writes_;
            /**
             * @brief How many times this node has been (re)dispatched to a worker.
             *
             * Incremented on every offload. After a worker death or a failed
             * remote result, the node is re-dispatched; once attempts exceed a
             * cap the coordinator pins it local (offloadable_ cleared) so a kernel
             * that consistently fails remotely cannot loop forever.
             */
            std::atomic<uint32_t> offload_attempts_{0};
#endif

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
                int32_t idx = inline_successor_count.fetch_add(1, std::memory_order_relaxed);
                if (idx < MAX_INLINE_SUCCESSORS)
                {
                    inline_successors[idx] = target;
                    return;
                }

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

            /** @brief Returns the execution mode for this node. */
            ExecutionMode get_execution_mode() const override   { return mode; }

            /** @brief Indicates that this task is a graph node. */
            bool is_graph_node() const noexcept override        { return true; }

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
