/****************************************************************/
/* dependency_tracker.hpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file dependency_tracker.hpp
 * @brief Declares DependencyTracker, which orders tasks by the regions they access.
 * @author Mustafa Garip
 * @see include/SushiRuntime/graph/README.md
 */

#pragma once

#include <array>
#include <atomic>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/resource_region.hpp>

namespace SushiRuntime 
{
    namespace Graph
    {
        struct Node; 

        /**
         * @brief Records a specific memory access event within the lock-free history linked list.
         *
         * Forms the basis for chronological dependency tracking by chaining read and write events together.
         */
        struct AccessNode
        {
            Node* task;
            bool is_write;
            SushiRuntime::Core::ResourceRegion region; /**< Byte interval this access touches. */
            AccessNode* prev; // Immutable once linked to the history
        };

        /**
         * @brief Manages the access history state for a single tracked resource using lock-free semantics.
         *
         * Maintains an atomic pointer to the most recent access event, aligned to hardware cache lines to prevent false sharing.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ResourceState 
        {
            /**
             * @brief Points atomically to the most recent access event in the history.
             *
             * Provides the necessary lock-free synchronization point for appending new accesses to the chain.
             */
            std::atomic<AccessNode*> tail{nullptr};
        };

        /**
         * @brief Resolves and manages task execution dependencies entirely without locks.
         *
         * Relies on a lock-free chronological access history distributed across independent resource shards to mitigate contention.
         */
        class SUSHIRUNTIME_API DependencyTracker 
        {
            private:
                /**
                 * @brief Defines the fixed number of concurrent resource tracking shards.
                 *
                 * Spreads memory addresses over multiple independent atomic roots to minimize cross-thread interference.
                 */
                static constexpr size_t SHARD_COUNT = 1024;
                
                /**
                 * @brief Stores the array of independent resource access states.
                 *
                 * Provides fixed-size contiguous memory containing all individual lock-free shard tails.
                 */
                std::array<ResourceState, SHARD_COUNT> registry;

                /**
                 * @brief Maps an allocation's identity to a shard index with a SplitMix64 hash.
                 * @see include/SushiRuntime/graph/README.md
                 */
                inline size_t get_shard_idx(SushiRuntime::Core::ResourceId base) const
                {
                    uint64_t z = reinterpret_cast<uint64_t>(base);
                    z += 0x9e3779b97f4a7c15;
                    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
                    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
                    return (z ^ (z >> 31)) % SHARD_COUNT;
                }

            public:
                /**
                 * @brief Constructs an empty dependency tracker instance.
                 *
                 * Initializes the internal registry of shards to a clean state.
                 */
                DependencyTracker() = default;

                /**
                 * @brief Reclaims any access history left behind on destruction.
                 * @see include/SushiRuntime/graph/README.md
                 */
                ~DependencyTracker() { reset(); }

                /**
                 * @brief Resets the dependency tracking states and reclaims memory.
                 *
                 * Safely traverses and deallocates all lock-free access history chains.
                 */
                void reset();

                /**
                 * @brief Links a task into the access dependency graph of a region by the
                 *        RAW/WAR/WAW rules.
                 * @param region The byte interval accessed; a bare base pointer names the whole
                 *        buffer.
                 */
                void resolve(Node* node, const SushiRuntime::Core::ResourceRegion& region, bool is_write);

                /**
                 * @brief Enforces an explicit execution ordering constraint between two tasks.
                 *
                 * Directly registers the source node as a required predecessor of the target node.
                 *
                 * @param from The predecessor task node that must complete first.
                 * @param to The successor task node whose execution is blocked.
                 */
                void add_dependency(Node* from, Node* to);
        };
        
    } // namespace Graph
} // namespace SushiRuntime