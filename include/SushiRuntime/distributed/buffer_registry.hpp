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
         * @brief Master-side bridge between local void* USM and serializable ids.
         *
         * The deliberate design choice (offline-first): the allocator does NOT
         * auto-assign ids. A buffer becomes distributable only when the user
         * explicitly registers it — an opt-in gesture meaningful only in Master
         * mode. The 8-of-10 jobs that never touch the cluster pay nothing here.
         *
         * The master is authoritative: it owns the current version of every
         * registered buffer and serves BUFFER_REQUESTs by reading the bytes
         * straight from the (host-accessible / shared) USM pointer.
         *
         * Registered buffers are expected to be shared USM so the host side can
         * read them when serving a pull. Device-only buffers should be staged to
         * shared USM before registration.
         */
        class SUSHIRUNTIME_API BufferRegistry
        {
            public:
                /**
                 * @brief Registers a USM pointer as a distributable buffer.
                 * @return The handle (id, version=1, size) for this buffer.
                 *
                 * Re-registering the same pointer returns its existing handle.
                 */
                BufferHandle register_buffer(void* usm_ptr, uint64_t size_bytes);

                /**
                 * @brief Bumps the version of a buffer the master just wrote.
                 *
                 * Driven by the DependencyTracker's WAW/WAR signal: any local
                 * write invalidates remote caches, expressed as a version bump.
                 * No-op for unregistered pointers.
                 */
                void bump_version(void* usm_ptr);

                /** @brief Current handle for a local pointer (used at compile time). */
                std::optional<BufferHandle> lookup(void* usm_ptr) const;

                /** @brief Resolves an id back to {ptr, handle} to serve a pull. */
                struct Entry { void* ptr; BufferHandle handle; };
                std::optional<Entry> lookup_by_id(GlobalBufferID id) const;

            private:
                mutable std::mutex mutex_;
                std::unordered_map<void*, BufferHandle>    by_ptr_;
                std::unordered_map<uint64_t, void*>        ptr_by_id_;
                uint64_t next_id_ = 1; /**< 0 is reserved for "invalid". */
        };

    } // namespace Distributed
} // namespace SushiRuntime
