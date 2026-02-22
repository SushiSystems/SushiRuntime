/**************************************************************************/
/* dependency_tracker.hpp                                                 */
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

#include <array>
#include <atomic>
#include <vector>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime 
{
    namespace Graph
    {
        struct Node; 

        /**
         * @brief State tracking for USM resources.
         * 
         * Uses cache line alignment to prevent false sharing.
         */
        
        /**
         * @brief A small vector-like container for Node pointers, using a static array
         *        for small capacities and falling back to a dynamic vector.
         */
        struct SmallNodeVector 
        {
            static constexpr size_t STATIC_CAP = 64;
            Node* static_data[STATIC_CAP];
            size_t static_count = 0;
            std::vector<Node*> dynamic_data;
            
            inline void push_back(Node* n) 
            {
                if (static_count < STATIC_CAP) 
                {
                    static_data[static_count++] = n;
                } 
                else 
                {
                    dynamic_data.push_back(n);
                }
            }
            
            inline void clear() 
            {
                static_count = 0;
                dynamic_data.clear();
            }
        };

        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ResourceState 
        {
            /** @brief Sharded spinlock for high performance. */
            std::atomic_flag lock = ATOMIC_FLAG_INIT;
            
            /** @brief Pointer to the last node that wrote to this resource. */
            Node* last_writer = nullptr; 
            
            /** @brief List of nodes currently reading from this resource. */
            SmallNodeVector readers;
            
            ResourceState() 
            {
                // No dynamic reserve needed for SmallNodeVector's static part.
                // Dynamic part will grow as needed.
            }
        };

        /**
         * @brief Tracks dependencies between tasks without using locks.
         * 
         * Uses static shards instead of an unordered_map for better speed.
         */
        class SUSHIRUNTIME_API DependencyTracker 
        {
            private:
                /** @brief Number of shards used to reduce contention. */
                static constexpr size_t SHARD_COUNT = 1024;
                /** @brief Registry of resource states. */
                std::array<ResourceState, SHARD_COUNT> registry;

                /**
                 * @brief Generates a slot index from a pointer address.
                 * @param ptr The pointer to map.
                 * @return size_t The index in the shard registry.
                 */
                inline size_t get_shard_idx(void* ptr) const 
                {
                    return (reinterpret_cast<size_t>(ptr) >> 6) % SHARD_COUNT;
                }

            public:
                /** @brief Initializes the dependency tracker. */
                DependencyTracker();

                /** @brief Clears all resources. */
                void reset();

                /**
                 * @brief Resolves implicit dependencies for a task node.
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
