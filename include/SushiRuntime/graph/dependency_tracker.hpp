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
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ResourceState 
        {
            /** @brief Sharded spinlock for high performance. */
            std::atomic_flag lock = ATOMIC_FLAG_INIT;
            
            /** @brief Pointer to the last node that wrote to this resource. */
            Node* last_writer = nullptr; 
            
            // TODO: Use a fixed-size array or object pool for readers instead of vector.
            /** @brief List of nodes currently reading from this resource. */
            std::vector<Node*> readers;
            
            ResourceState() 
            {
                // TODO: Avoid dynamic reserve here.
                readers.reserve(16);
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
