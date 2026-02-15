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
#include <vector>
#include <functional>
#include <optional>
#include <memory>
#include "../scheduler/splittable_task.hpp"
#include "../core/ref_counted.hpp"

namespace SushiRuntime 
{
    namespace Graph 
    {
        /** @brief work definition for a task node. */
        using TaskWork = std::function<void(sycl::handler&)>;

        struct Node;

        /**
         * @brief element for a lock-free linked list of successor nodes.
         */
        struct SuccessorNode 
        {
            Node* target;
            SuccessorNode* next;
        };

        /**
         * @brief representing a node in the runtime graph. 
         * 
         * it uses lock-free dependency management and inherits from SplittableTask.
         */
        struct Node : public SushiRuntime::Scheduler::SplittableTask
        {
            /** @brief the work to be executed in this node. */
            TaskWork work;
            
            /** @brief number of incoming dependencies. */
            std::atomic<int32_t> in_degree{0};
            
            /** @brief head of the lock-free successor list. */
            std::atomic<SuccessorNode*> successor_list{nullptr};
            
            /** @brief sycl events this node depends on. */
            std::vector<sycl::event> sycl_deps;
            
            /** @brief flag to indicate if the node is fused with others. */
            bool is_fused = false;

            /**
             * @brief adds a successor node in a lock-free way.
             * @param target the node that depends on this node.
             */
            void add_successor(Node* target) noexcept 
            {
                // TODO: use an object pool for successor nodes to avoid frequent new/delete operations.
                SuccessorNode* new_node = new SuccessorNode{target, nullptr};
                SuccessorNode* old_head = successor_list.load(std::memory_order_relaxed);
                do 
                {
                    new_node->next = old_head;
                } while (!successor_list.compare_exchange_weak(old_head, new_node, 
                                                              std::memory_order_release, 
                                                              std::memory_order_relaxed));
            }

            /** @brief executes the task work within a sycl handler. */
            void execute(sycl::handler& h) override 
            {
                if(work) work(h);
            }

            /** @brief optionally splits the task into smaller subtasks for better load balancing. */
            std::optional<sushi_ptr<SushiRuntime::Scheduler::SplittableTask>> split() override 
            {
                return std::nullopt;
            }

            /** @brief returns true if the task can be split. */
            bool is_splittable() const override 
            {
                return false;
            }

            /** @brief destructor cleans up the atomic successor list. */
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
