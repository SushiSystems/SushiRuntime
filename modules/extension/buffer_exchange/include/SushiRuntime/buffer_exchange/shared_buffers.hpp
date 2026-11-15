/**************************************************************************/
/* shared_buffers.hpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file shared_buffers.hpp
 * @brief Declares SharedBuffers, the versions of the buffers the master shares, by GlobalHandle.
 * @author Mustafa Garip
 * @see modules/extension/buffer_exchange/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names how read() or write() ended.
         */
        enum class BufferAccessStatus : std::uint8_t
        {
            Ok,         /**< The bytes were copied. */
            Stale,      /**< The buffer's version is not the one named; nothing was copied. */
            Gone,       /**< The id names no shared buffer or no live instance; nothing was copied. */
            OutOfRange, /**< Offset plus length passes the buffer's size; nothing was copied. */
        };

        /**
         * @brief Gives each shared buffer a version and copies its bytes; a buffer is a GlobalHandle.
         *
         * The wire carries the handle itself. The set keeps no address: a
         * copy asks the HandleResolver where the instance is. One mutex, held only inside the
         * members, covers each copy, so a buffer cannot be unshared in the middle of one.
         */
        class SUSHIRUNTIME_API SharedBuffers final
        {
            public:
                /**
                 * @brief Builds an empty set that finds instances through a resolver.
                 * @param resolver Outlives the set.
                 * @param device_count Devices whose memory space may hold an instance.
                 */
                SharedBuffers(const HandleResolver& resolver, std::size_t device_count) noexcept;

                /** @brief Destroys the set; the buffers are not its own. */
                ~SharedBuffers();

                SharedBuffers(const SharedBuffers&) = delete;
                SharedBuffers& operator=(const SharedBuffers&) = delete;
                SharedBuffers(SharedBuffers&&) = delete;
                SharedBuffers& operator=(SharedBuffers&&) = delete;

                /**
                 * @brief Shares a buffer; one already shared keeps its version.
                 * @param handle A live handle whose instance the host can read.
                 * @return The buffer's wire handle; the null handle as id for the null handle.
                 */
                BufferHandle share(GlobalHandle handle, std::uint64_t size_bytes);

                /**
                 * @brief Stops sharing a buffer, under the mutex, so no copy is in progress when
                 *        this returns.
                 * @return The handle, or the null handle when it was not shared.
                 */
                GlobalHandle unshare(GlobalHandle handle) noexcept;

                /** @brief Returns the current wire handle of a buffer, or nothing. */
                [[nodiscard]] std::optional<BufferHandle> lookup(GlobalHandle handle) const;

                /**
                 * @brief Writes the current wire handle of each buffer into @p out, under one lock.
                 * @param out Cleared first; its capacity is reused.
                 * @return False when a handle is not shared; @p out is then unspecified.
                 */
                [[nodiscard]] bool resolve(SushiRuntime::span<const GlobalHandle> handles,
                                           std::vector<BufferHandle>& out) const;

                /**
                 * @brief Rewrites each wire handle to its buffer's current version, under one lock.
                 * @return False when an id is gone; the handles are then unspecified.
                 */
                [[nodiscard]] bool refresh(SushiRuntime::span<BufferHandle> handles) const;

                /**
                 * @brief Raises the version of each buffer by one and writes it into the wire
                 *        handle, under one lock; an id that is gone is skipped.
                 */
                void bump(SushiRuntime::span<BufferHandle> handles) noexcept;

                /** @brief Raises the version of each buffer in @p handles that is shared. */
                void bump_handles(SushiRuntime::span<const GlobalHandle> handles) noexcept;

                /**
                 * @brief Copies bytes out of a buffer while it is at exactly @p version.
                 * @param out Its size is the number of bytes wanted; 0 only checks.
                 */
                [[nodiscard]] BufferAccessStatus read(GlobalHandle id, std::uint32_t version,
                                                      std::uint64_t offset,
                                                      SushiRuntime::span<std::byte> out) const;

                /**
                 * @brief Copies bytes into a buffer while it is at exactly @p version; the
                 *        second half of the output fence. The version does not change.
                 */
                [[nodiscard]] BufferAccessStatus write(GlobalHandle id, std::uint32_t version,
                                                       std::uint64_t offset,
                                                       SushiRuntime::span<const std::byte> in);

            private:
                /** @brief Holds which buffer an entry is and its current wire handle. */
                struct Entry
                {
                    /** @brief Handle, current version and size. */
                    BufferHandle wire;
                };

                /**
                 * @brief Returns the host address of an entry's instance, or null for none.
                 * @pre The mutex is held.
                 */
                [[nodiscard]] void* address_of(const Entry& entry) const noexcept;

                /// @brief Finds where an instance lives; outlives the set.
                const HandleResolver& resolver_;

                /// @brief Devices the resolver may hold an instance on.
                std::size_t device_count_;

                /// @brief Guards every member below; held across each copy.
                mutable std::mutex mutex_;

                /// @brief Entries by the handle that names the buffer.
                std::unordered_map<GlobalHandle, Entry> by_handle_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
