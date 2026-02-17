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
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/scheduler/task.hpp>

namespace SushiRuntime 
{
    namespace Graph 
    {
        /** @brief Work definition for a task node. */
        using TaskWork = std::function<void(sycl::handler&)>;

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
            /** @brief The work to be executed in this node. */
            TaskWork work;
            
            /** @brief Number of incoming dependencies. */
            std::atomic<int32_t> in_degree{0};
            
            /** @brief Head of the lock-free successor list. */
            std::atomic<SuccessorNode*> successor_list{nullptr};
            
            // TODO: Replace with a fixed-size array or small-vector to avoid allocations per node.
            /** @brief SYCL events this node depends on. */
            std::vector<sycl::event> sycl_deps;
            
            /** @brief Flag to indicate if the node is fused with others. */
            bool is_fused = false;

            /**
             * @brief Adds a successor node in a lock-free way.
             * @param target The node that depends on this node.
             */
            void add_successor(Node* target) noexcept 
            {
                // TODO: use an object pool for successor nodes to avoid frequent new/delete operations.
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

            /** @brief Executes the task work within a SYCL handler. */
            void execute(sycl::handler& h) override 
            {
                if(work) work(h);
            }

            /** @brief Optionally splits the task into smaller subtasks for better load balancing. */
            std::optional<sushi_ptr<SushiRuntime::Scheduler::Task>> split() override 
            {
                return std::nullopt;
            }

            /** @brief Returns true if the task can be split. */
            bool is_splittable() const override 
            {
                return false;
            }

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
