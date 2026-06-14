/**************************************************************************/
/* node.hpp                                                               */
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

#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <optional>
#include <functional>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#ifdef SUSHI_DISTRIBUTED
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

        struct SmallEventVector 
        {
            static constexpr size_t STATIC_CAP = 8;

            size_t static_count = 0;
            sycl::event static_data[STATIC_CAP];
            std::vector<sycl::event> dynamic_data;
            
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
         * @brief Element for a lock-free linked list of successor nodes.
         */
        struct SuccessorNode 
        {
            Node* target;
            SuccessorNode* next;
        };

        /**
         * @brief Representing a node in the runtime graph. 
         * 
         * It uses lock-free dependency management and inherits from Task.
         */
        struct Node : public SushiRuntime::Scheduler::Task
        {
            /** @brief The semantic metadata defining this task. */
            TaskMetadata metadata;

            /** @brief The work to be executed in this node. */
            TaskWork work;
            
            /** @brief The host work to be executed in this node if it's a HOST task. */
            HostWork host_work;
            
            /** @brief The execution mode of this node. */
            ExecutionMode mode = ExecutionMode::KERNEL;

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
             * @brief Raw USM read/write pointers this node touches.
             *
             * Retained on every build (not just distributed) so the scheduler
             * can route a KERNEL submit to the queue whose context actually owns
             * the data. USM is pinned to the device/context it was allocated on;
             * running the kernel on a sibling device's queue (e.g. CPU USM on a
             * CUDA queue) is an illegal cross-context access. See
             * TaskScheduler::execute_task.
             */
            std::vector<void*> io_reads_;
            std::vector<void*> io_writes_;

#ifdef SUSHI_DISTRIBUTED
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
             * @brief Adds a successor node in a lock-free way.
             * @param target The node that depends on this node.
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
