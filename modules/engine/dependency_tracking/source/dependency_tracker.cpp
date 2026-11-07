/**************************************************************************/
/* dependency_tracker.cpp                                                 */
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
 * @file dependency_tracker.cpp
 * @brief Resolves read and write hazards between graph nodes from a lock-free access history.
 * @author Mustafa Garip
 */

#include <thread>
#include <algorithm>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/dependency_tracking/dependency_tracker.hpp>

namespace SushiRuntime 
{
    namespace Internal::Graph
    {
        DependencyTracker::DependencyTracker()
            : owned_epochs_(std::make_unique<SushiRuntime::Internal::Memory::EpochManager>()),
              epochs_(owned_epochs_.get())
        {

        }

        DependencyTracker::DependencyTracker(SushiRuntime::Internal::Memory::EpochManager& epochs)
            : epochs_(&epochs)
        {

        }

        DependencyTracker::~DependencyTracker()
        {
            reset();
        }

        /**
         * @brief Resets the dependency tracker state.
         *
         * Cleans up the lock-free access history across all shards. It is safe
         * to run after graph compilation completes.
         */
        void DependencyTracker::reset()
        {
            // reset() runs only after graph compilation has finished (modules/engine/graph/README.md).
            for (auto& state : registry)
            {
                AccessNode* curr = state.tail.exchange(nullptr, std::memory_order_acquire);
                while (curr) 
                {
                    AccessNode* prev = curr->prev;
                    epochs_->retire(curr);
                    curr = prev;
                }
            }
        }

        /**
         * @brief Orders a node after every earlier overlapping access (modules/engine/graph/README.md).
         * @param node The graph node introducing the access.
         * @param region The byte interval being accessed (whole buffer by default).
         * @param is_write True if the access is a write, false if it is a read.
         */
        void DependencyTracker::resolve(Node* node, const SushiRuntime::BufferRegion& region, bool is_write)
        {
            SushiRuntime::Internal::Memory::EpochGuard guard(*epochs_);

            size_t idx = shard_index(region.base);
            auto& state = registry[idx];

            AccessNode* current = new AccessNode{node, is_write, region, nullptr};
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
                // WAW and WAR: every overlapping prior access orders us (modules/engine/graph/README.md).
                while (cursor)
                {
                    if (cursor->region.overlaps(region))
                    {
                        if (cursor->task != node)
                            add_dependency(cursor->task, node);

                        if (cursor->is_write && cursor->region.contains(region))
                            break; // Region fully covered; earlier accesses are ordered behind this writer.
                    }
                    cursor = cursor->prev;
                }
            }
            else
            {
                // RAW: every overlapping prior write orders us (modules/engine/graph/README.md).
                while (cursor)
                {
                    if (cursor->is_write && cursor->region.overlaps(region))
                    {
                        if (cursor->task != node)
                            add_dependency(cursor->task, node);

                        if (cursor->region.contains(region))
                            break; // The region's writes are fully accounted for.
                    }
                    cursor = cursor->prev;
                }
            }
        }

        /**
         * @brief Adds a successor edge between two nodes unless it exists (modules/engine/graph/README.md).
         * @param from The source node.
         * @param to The target node that depends on the source node.
         */
        void DependencyTracker::add_dependency(Node* from, Node* to)
        {
            if (from && to && from != to)
            {
                // Check inline list to avoid duplicates
                int32_t count = from->inline_successor_count.load(std::memory_order_relaxed);
                int32_t max_idx = std::min(count, Node::K_MAX_INLINE_SUCCESSORS);
                
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