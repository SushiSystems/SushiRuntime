/**************************************************************************/
/* dependency_tracker.cpp                                                 */
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

#include <thread>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/graph/dependency_tracker.hpp>

namespace SushiRuntime 
{
    namespace Graph
    {
        DependencyTracker::DependencyTracker() 
        {
            /** @brief Initializing resources. */
        }

        /**
         * @brief Resolves memory dependencies for a task node.
         * 
         * Handles RAW, WAR, and WAW dependencies using a sharded spinlock.
         * 
         * @param node The task node to check.
         * @param ptr The memory address being accessed.
         * @param is_write True if the task writes to the memory.
         */
        void DependencyTracker::resolve(Node* node, void* ptr, bool is_write) 
        {
            size_t idx = get_shard_idx(ptr);
            auto& state = registry[idx];

            /** @brief Busy-wait loop for the sharded spinlock. */
            while(state.lock.test_and_set(std::memory_order_acquire)) 
            {
                // TODO: use assembly 'pause' instruction for lower cpu power usage.
                std::this_thread::yield(); 
            }

            if (is_write) 
            {
                // waw: track dependency from previous writer to this node
                if (state.last_writer && state.last_writer != node) 
                {
                    add_dependency(state.last_writer, node);
                }

                // war: track dependency from all readers to this writer
                for(Node* reader : state.readers) 
                {
                    if(reader != node) add_dependency(reader, node);
                }
                
                state.readers.clear();
                state.last_writer = node;
            } 
            else 
            {
                // raw: track dependency from the last writer to this reader
                if (state.last_writer && state.last_writer != node) 
                {
                    add_dependency(state.last_writer, node);
                }
                
                // TODO: switch to a small_vector or fixed-size array to avoid heap allocation.
                state.readers.push_back(node);
            }

            state.lock.clear(std::memory_order_release);
        }

        /**
         * @brief Adds a direct dependency between two graph nodes.
         * @param from The node that must finish first.
         * @param to The node that depends on the first one.
         */
        void DependencyTracker::add_dependency(Node* from, Node* to)
        {
            if (from && to)
            {
                /** @brief Use atomic successor list to avoid locks. */
                from->add_successor(to);
                to->in_degree.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
    } // namespace Graph
} // namespace SushiRuntime