/**************************************************************************/
/* buffer_registry.hpp                                                    */
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

#include <mutex>
#include <optional>
#include <unordered_map>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/protocol.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Master-side bridge between local void* USM pointers and serializable buffer IDs.
         *
         * Allows sharing local memory across the cluster by explicitly registering pointers.
         * The master owns the authoritative version of every registered buffer and serves remote requests by reading directly from host-accessible memory.
         */
        class SUSHIRUNTIME_API BufferRegistry
        {
            public:
                /**
                 * @brief Registers a USM pointer as a distributable buffer.
                 * 
                 * Re-registering an already known pointer returns its existing handle.
                 * 
                 * @param usm_ptr The local shared memory pointer to expose to the cluster.
                 * @param size_bytes The size of the buffer in bytes.
                 * @return The resulting handle containing the assigned ID, initial version, and size.
                 */
                BufferHandle register_buffer(void* usm_ptr, uint64_t size_bytes);

                /**
                 * @brief Bumps the version of a buffer after a local master write.
                 *
                 * Invalidates remote worker caches by incrementing the buffer's version. Takes no action for unregistered pointers.
                 *
                 * @param usm_ptr The local pointer that was modified.
                 */
                void bump_version(void* usm_ptr);

                /**
                 * @brief Bumps a buffer's version using its globally unique ID.
                 *
                 * Allows the coordinator to invalidate caches during task dispatch without resolving local pointers. Takes no action for unknown IDs.
                 *
                 * @param id The global ID of the buffer to bump.
                 */
                void bump_version_by_id(GlobalBufferID id);

                /**
                 * @brief Unregisters a distributable buffer.
                 *
                 * Removes the mapping to prevent a future reallocation at the same memory address from aliasing an old buffer ID.
                 *
                 * @param usm_ptr The local pointer being freed or unshared.
                 */
                void unregister_buffer(void* usm_ptr);

                /**
                 * @brief Retrieves the current handle for a registered local pointer.
                 *
                 * Queries the registry during graph compilation to link local buffers to distributed IDs.
                 *
                 * @param usm_ptr The local pointer to query.
                 * @return The associated buffer handle if registered, or std::nullopt if unknown.
                 */
                std::optional<BufferHandle> lookup(void* usm_ptr) const;

                /**
                 * @brief Associates a resolved local pointer with its distributed handle.
                 * 
                 * Used internally to package local memory for remote transmission.
                 */
                struct Entry { void* ptr; BufferHandle handle; };

                /**
                 * @brief Resolves a global buffer ID back to its local pointer and handle.
                 *
                 * Provides the memory address and size required to serve remote pull requests.
                 *
                 * @param id The global ID of the requested buffer.
                 * @return The local entry if the ID exists, or std::nullopt otherwise.
                 */
                std::optional<Entry> lookup_by_id(GlobalBufferID id) const;

            private:
                mutable std::mutex mutex_;
                std::unordered_map<void*, BufferHandle>    by_ptr_;
                std::unordered_map<uint64_t, void*>        ptr_by_id_;
                uint64_t next_id_ = 1; /**< 0 is reserved for "invalid". */
        };

    } // namespace Distributed
} // namespace SushiRuntime
