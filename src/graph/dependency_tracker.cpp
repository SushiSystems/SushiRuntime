/****************************************************************/
/* dependency_tracker.cpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#include <thread>
#include <algorithm>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/graph/dependency_tracker.hpp>

namespace SushiRuntime 
{
    namespace Graph
    {
        /**
         * @brief Resets the dependency tracker state.
         *
         * Cleans up the lock-free access history across all shards. It is safe
         * to run after graph compilation completes.
         */
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
         *
         * Inserts the access into the lock-free history and traverses backwards,
         * ordering this node after every earlier access whose region overlaps,
         * to resolve RAW, WAR, and WAW hazards. The walk stops early only once a
         * prior writer is found that fully covers the accessed region: every
         * earlier access to that range is then already ordered behind that writer
         * transitively, so it needs no direct edge. A partially-overlapping writer
         * does not end the walk, because an earlier access could touch the part of
         * the region it leaves uncovered — stopping there would drop a real edge
         * and silently race on sub-regions.
         *
         * @param node The graph node introducing the access.
         * @param region The byte interval being accessed (whole buffer by default).
         * @param is_write True if the access is a write, false if it is a read.
         */
        void DependencyTracker::resolve(Node* node, const SushiRuntime::Core::ResourceRegion& region, bool is_write)
        {
            SushiRuntime::Memory::EpochGuard guard;

            size_t idx = get_shard_idx(region.base);
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
                // WAW and WAR: order after every overlapping prior access (read or
                // write), stopping at the first prior writer that fully covers us.
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
                // RAW: order after every overlapping prior write (readers stay
                // parallel), stopping once a covering writer is found.
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
         * @brief Explicitly adds a dependency between two nodes.
         *
         * Registers a successor relationship and updates the target node's
         * in-degree using atomic operations.
         *
         * @param from The source node.
         * @param to The target node that depends on the source node.
         */
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