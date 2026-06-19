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

namespace SushiRuntime 
{
    namespace Graph
    {
        struct Node; 

        /**
         * @brief Represents a single memory access event in the lock-free history.
         */
        struct AccessNode 
        {
            Node* task;
            bool is_write;
            void* ptr;
            AccessNode* prev; // Immutable once linked to the history
        };

        /**
         * @brief State tracking for USM resources using Lock-Free Access History.
         * * Uses cache line alignment to prevent false sharing.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ResourceState 
        {
            /** @brief Lock-free atomic pointer to the most recent access. */
            std::atomic<AccessNode*> tail{nullptr};
        };

        /**
         * @brief Tracks dependencies between tasks completely without using locks.
         * * Eliminates spinlocks by utilizing a chronological lock-free linked list
         * (Access History) for each resource shard.
         */
        class SUSHIRUNTIME_API DependencyTracker 
        {
            private:
                /** @brief Number of shards used to reduce contention. */
                static constexpr size_t SHARD_COUNT = 1024;
                
                /** @brief Registry of resource states. */
                std::array<ResourceState, SHARD_COUNT> registry;

                /**
                 * @brief Generates a slot index from a pointer address using SplitMix64 hash.
                 * @param ptr The pointer to map.
                 * @return size_t The index in the shard registry.
                 */
                inline size_t get_shard_idx(void* ptr) const 
                {
                    uint64_t z = reinterpret_cast<uint64_t>(ptr);
                    z += 0x9e3779b97f4a7c15;
                    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
                    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
                    return (z ^ (z >> 31)) % SHARD_COUNT;
                }

            public:
                /** @brief Initializes the dependency tracker. */
                DependencyTracker() = default;

                /** @brief Cleans up the access history objects safely. */
                void reset();

                /**
                 * @brief Resolves implicit dependencies for a task node utilizing Lock-Free traversal.
                 * @param node The task node to check.
                 * @param ptr The memory resource being accessed.
                 * @param is_write True if the access is a write operation.
                 */
                void resolve(Node* node, void* ptr, bool is_write);

                /**
                 * @brief Creates a direct dependency between two nodes.
                 * @param from The source node.
                 * @param to The target node.
                 */
                void add_dependency(Node* from, Node* to);
        };
        
    } // namespace Graph
} // namespace SushiRuntime