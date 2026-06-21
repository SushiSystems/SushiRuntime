/**************************************************************************/
/* buffer_registry.cpp                                                    */
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

#include <SushiRuntime/distributed/buffer_registry.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Registers a USM memory buffer with the distributed registry to track its state and version.
         * Assigns a new unique ID or increments the version if the pointer was previously registered.
         * @param usm_ptr The memory pointer to track.
         * @param size_bytes The size of the memory buffer in bytes.
         * @return A BufferHandle containing the registration details.
         */
        BufferHandle BufferRegistry::register_buffer(void* usm_ptr, uint64_t size_bytes)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Re-registering a live pointer means the USM address was handed back
            // out (allocator reuse): keep the stable id but bump the version so any
            // worker caching the old bytes treats its copy as stale.
            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
            {
                ++it->second.version;
                return it->second;
            }

            BufferHandle h;
            h.id.value    = next_id_++;
            h.version     = 1;
            h.size_bytes  = size_bytes;

            by_ptr_[usm_ptr]      = h;
            ptr_by_id_[h.id.value] = usm_ptr;
            return h;
        }

        /**
         * @brief Increments the version of a registered buffer.
         * Locates the buffer by its pointer and updates the version count to invalidate stale copies.
         * @param usm_ptr The memory pointer associated with the buffer.
         */
        void BufferRegistry::bump_version(void* usm_ptr)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
                ++it->second.version;
        }

        /**
         * @brief Increments the version of a registered buffer using its global ID.
         * Locates the buffer by its ID and updates the version count to invalidate stale copies.
         * @param id The GlobalBufferID identifying the buffer.
         */
        void BufferRegistry::bump_version_by_id(GlobalBufferID id)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto pit = ptr_by_id_.find(id.value);
            if (pit == ptr_by_id_.end())
                return;
            if (auto it = by_ptr_.find(pit->second); it != by_ptr_.end())
                ++it->second.version;
        }

        /**
         * @brief Removes a buffer from the registry.
         * Erases the internal tracking data associated with the given pointer.
         * @param usm_ptr The memory pointer to unregister.
         */
        void BufferRegistry::unregister_buffer(void* usm_ptr)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
            {
                ptr_by_id_.erase(it->second.id.value);
                by_ptr_.erase(it);
            }
        }

        /**
         * @brief Retrieves the buffer handle associated with a specific pointer.
         * Looks up the pointer in the registry and returns its handle if found.
         * @param usm_ptr The memory pointer to look up.
         * @return An std::optional containing the BufferHandle if found, or std::nullopt otherwise.
         */
        std::optional<BufferHandle> BufferRegistry::lookup(void* usm_ptr) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = by_ptr_.find(usm_ptr); it != by_ptr_.end())
                return it->second;
            return std::nullopt;
        }

        /**
         * @brief Retrieves the buffer entry associated with a specific global ID.
         * Looks up the ID in the registry and returns its corresponding entry.
         * @param id The GlobalBufferID to look up.
         * @return An std::optional containing the BufferRegistry::Entry if found, or std::nullopt otherwise.
         */
        std::optional<BufferRegistry::Entry> BufferRegistry::lookup_by_id(GlobalBufferID id) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto pit = ptr_by_id_.find(id.value);
            if (pit == ptr_by_id_.end())
                return std::nullopt;

            auto hit = by_ptr_.find(pit->second);
            if (hit == by_ptr_.end())
                return std::nullopt;

            return Entry{ pit->second, hit->second };
        }

    } // namespace Distributed
} // namespace SushiRuntime
