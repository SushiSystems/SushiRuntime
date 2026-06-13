/**************************************************************************/
/* buffer_registry.hpp                                                    */
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
