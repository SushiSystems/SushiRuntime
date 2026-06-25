/**************************************************************************/
/* dependency_tracker.hpp                                                 */
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
                 * @brief Maps an allocation's identity to a specific internal shard index.
                 *
                 * Shards on the owning allocation (its base), not the sub-region, so
                 * every access to one allocation lands in the same chain and can be
                 * compared by interval overlap. Applies a fast SplitMix64 bitwise hash
                 * to distribute allocations uniformly.
                 *
                 * @param base The owning allocation identity to hash.
                 * @return The computed array index corresponding to a specific shard.
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
                 * @brief Resets the dependency tracking states and reclaims memory.
                 *
                 * Safely traverses and deallocates all lock-free access history chains.
                 */
                void reset();

                /**
                 * @brief Links a task into the implicit access dependency graph for a region.
                 *
                 * Appends a new access node to the owning allocation's shard and wires
                 * predecessor constraints to every earlier access whose region overlaps
                 * @p region, following the RAW/WAR/WAW data-flow rules.
                 *
                 * @param node The current task node that requires execution ordering.
                 * @param region The byte interval of the resource being accessed. A bare
                 *               base pointer converts to the whole-buffer region.
                 * @param is_write Indicates whether the operation intends to modify the resource.
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