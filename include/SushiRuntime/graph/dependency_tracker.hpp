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
#include <atomic>
#include <array>
#include <vector>
#include "node.hpp"
#include "../core/common.hpp"

namespace SushiRuntime 
{
    namespace Graph
    {
        struct Node; 

        /**
         * @brief state tracking for usm resources.
         * 
         * uses cache line alignment to prevent false sharing.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ResourceState 
        {
            /** @brief sharded spinlock for high performance. */
            std::atomic_flag lock = ATOMIC_FLAG_INIT;
            
            /** @brief pointer to the last node that wrote to this resource. */
            Node* last_writer = nullptr; 
            
            /** @brief list of nodes currently reading from this resource. */
            std::vector<Node*> readers;
            
            ResourceState() 
            {
                readers.reserve(16);
            }
        };

        /**
         * @brief tracks dependencies between tasks without using locks.
         * 
         * uses static shards instead of an unordered_map for better speed.
         */
        class DependencyTracker 
        {
            private:
                /** @brief number of shards used to reduce contention. */
                static constexpr size_t SHARD_COUNT = 1024;
                /** @brief registry of resource states. */
                std::array<ResourceState, SHARD_COUNT> registry;

                /**
                 * @brief generates a slot index from a pointer address.
                 * @param ptr the pointer to map.
                 * @return size_t the index in the shard registry.
                 */
                inline size_t get_shard_idx(void* ptr) const 
                {
                    return (reinterpret_cast<size_t>(ptr) >> 6) % SHARD_COUNT;
                }

            public:
                /** @brief initializes the dependency tracker. */
                DependencyTracker();

                /**
                 * @brief resolves implicit dependencies for a task node.
                 * @param node the task node to check.
                 * @param ptr the memory resource being accessed.
                 * @param is_write true if the access is a write operation.
                 */
                void resolve(Node* node, void* ptr, bool is_write);

                /**
                 * @brief creates a direct dependency between two nodes.
                 * @param from the source node.
                 * @param to the target node.
                 */
                void add_dependency(Node* from, Node* to);
        };
        
    } // namespace Graph
} // namespace SushiRuntime
