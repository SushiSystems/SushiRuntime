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
#include <algorithm>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/graph/dependency_tracker.hpp>

namespace SushiRuntime 
{
    namespace Graph
    {
        void DependencyTracker::reset()
        {
            // Clean up the lock-free access history for all shards
            // Safe to run because reset() is called when the graph compilation is completely done.
            for (auto& state : registry)
            {
                AccessNode* curr = state.tail.exchange(nullptr, std::memory_order_acquire);
                while (curr) 
                {
                    AccessNode* prev = curr->prev;
                    SushiRuntime::Memory::EpochManager::instance().retire(curr);
                    curr = prev;
                }
            }
        }

        /**
         * @brief Resolves memory dependencies for a task node.
         * * Inserts the access into the lock-free history and traverses backwards
         * to automatically resolve RAW, WAR, and WAW hazards.
         */
        void DependencyTracker::resolve(Node* node, void* ptr, bool is_write) 
        {
            SushiRuntime::Memory::EpochGuard guard;
            
            size_t idx = get_shard_idx(ptr);
            auto& state = registry[idx];

            AccessNode* current = new AccessNode{node, is_write, nullptr};
            AccessNode* old_tail = state.tail.load(std::memory_order_relaxed);
            
            // 1. Lock-Free Push to the chronologic access history
            do 
            {
                current->prev = old_tail;
            } 
            while (!state.tail.compare_exchange_weak(old_tail, current, 
                                                     std::memory_order_acq_rel, 
                                                     std::memory_order_relaxed));

            // 2. Lock-Free Traversal to build the dependency edges
            AccessNode* cursor = current->prev;

            if (is_write) 
            {
                // WAW and WAR: Depend on all previous reads until we hit the first previous write.
                while (cursor) 
                {
                    if (cursor->task != node)
                        add_dependency(cursor->task, node);
                    
                    if (cursor->is_write) break; // Found the preceding writer, stop traversing.
                    
                    cursor = cursor->prev;
                }
            } 
            else 
            {
                // RAW: Depend only on the most recent write. Skip all previous reads.
                while (cursor) 
                {
                    if (cursor->is_write) 
                    {
                        if (cursor->task != node) 
                            add_dependency(cursor->task, node);

                        break; // Found the preceding writer, stop traversing.
                    }
                    cursor = cursor->prev;
                }
            }
        }

        void DependencyTracker::add_dependency(Node* from, Node* to)
        {
            if (from && to && from != to)
            {
                // Check inline list to avoid duplicates
                int32_t count = from->inline_successor_count.load(std::memory_order_relaxed);
                int32_t max_idx = std::min(count, Node::MAX_INLINE_SUCCESSORS);
                
                for (int32_t i = 0; i < max_idx; ++i) 
                    if (from->inline_successors[i] == to) return;

                // Check spillover list for duplicates
                SuccessorNode* curr = from->successor_list.load(std::memory_order_relaxed);
                while (curr) 
                {
                    if (curr->target == to) return;
                    curr = curr->next;
                }

                /** @brief Use atomic successor list to avoid locks. */
                from->add_successor(to);
                to->in_degree.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
    } // namespace Graph
} // namespace SushiRuntime