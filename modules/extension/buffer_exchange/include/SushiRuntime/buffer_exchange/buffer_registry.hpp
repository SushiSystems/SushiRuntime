/**************************************************************************/
/* buffer_registry.hpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file buffer_registry.hpp
 * @brief Declares BufferRegistry, the master's map of shared buffers and their versions.
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
            Gone,       /**< The id names no shared buffer; nothing was copied. */
            OutOfRange, /**< Offset plus length passes the buffer's size; nothing was copied. */
        };

        /**
         * @brief Gives each shared allocation an id and a version, and copies its bytes.
         *
         * The registry owns one mutex, held only inside its members, and calls nothing while
         * it holds it. No address leaves the registry: read() and write() are the only code
         * that touches a shared buffer's memory for a worker, and they copy under the mutex,
         * so a buffer cannot be unregistered and freed in the middle of a copy.
         */
        class SUSHIRUNTIME_API BufferRegistry final
        {
            public:
                /** @brief Builds an empty registry whose first id is 1. */
                BufferRegistry();

                /** @brief Destroys the registry; the allocations are not its own. */
                ~BufferRegistry();

                BufferRegistry(const BufferRegistry&) = delete;
                BufferRegistry& operator=(const BufferRegistry&) = delete;
                BufferRegistry(BufferRegistry&&) = delete;
                BufferRegistry& operator=(BufferRegistry&&) = delete;

                /**
                 * @brief Shares an allocation; an address already shared keeps its handle.
                 * @param address Base of a live, host-readable USM allocation of @p size_bytes.
                 */
                BufferHandle register_buffer(void* address, std::uint64_t size_bytes);

                /**
                 * @brief Stops sharing an address, under the mutex, so no copy is in progress
                 *        when this returns.
                 * @return The id the address had, or an invalid id when it was not shared.
                 */
                GlobalBufferID unregister_buffer(void* address) noexcept;

                /** @brief Returns the current handle of an address, or nothing. */
                [[nodiscard]] std::optional<BufferHandle> lookup(void* address) const;

                /** @brief Returns the current handle of an id, or nothing. */
                [[nodiscard]] std::optional<BufferHandle> lookup_by_id(GlobalBufferID id) const;

                /**
                 * @brief Writes the current handle of each address into @p out, under one lock.
                 * @param out Cleared first; its capacity is reused.
                 * @return False when an address is not shared; @p out is then unspecified.
                 */
                [[nodiscard]] bool resolve(SushiRuntime::span<void* const> addresses,
                                           std::vector<BufferHandle>& out) const;

                /**
                 * @brief Rewrites each handle to its buffer's current version, under one lock.
                 * @return False when an id is gone; the handles are then unspecified.
                 */
                [[nodiscard]] bool refresh(SushiRuntime::span<BufferHandle> handles) const;

                /**
                 * @brief Raises the version of each buffer by one and writes it into the handle,
                 *        under one lock; an id that is gone is skipped.
                 */
                void bump(SushiRuntime::span<BufferHandle> handles) noexcept;

                /** @brief Raises the version of the buffer shared at each address, if any. */
                void bump_addresses(SushiRuntime::span<void* const> addresses) noexcept;

                /**
                 * @brief Copies bytes out of a buffer while it is at exactly @p version.
                 * @param out Its size is the number of bytes wanted; 0 only checks.
                 */
                [[nodiscard]] BufferAccessStatus read(GlobalBufferID id, std::uint32_t version,
                                                      std::uint64_t offset,
                                                      SushiRuntime::span<std::byte> out) const;

                /**
                 * @brief Copies bytes into a buffer while it is at exactly @p version; the
                 *        second half of the output fence. The version does not change.
                 */
                [[nodiscard]] BufferAccessStatus write(GlobalBufferID id, std::uint32_t version,
                                                       std::uint64_t offset,
                                                       SushiRuntime::span<const std::byte> in);

            private:
                /** @brief Holds where a shared buffer lives and its current handle. */
                struct Entry
                {
                    /** @brief Base address of the allocation. */
                    void* address = nullptr;

                    /** @brief Id, current version and size. */
                    BufferHandle handle;
                };

                /// @brief Guards every member below; held across each copy.
                mutable std::mutex mutex_;

                /// @brief Entries by the value of their id.
                std::unordered_map<std::uint64_t, Entry> by_id_;

                /// @brief Id values by address.
                std::unordered_map<void*, std::uint64_t> id_by_address_;

                /// @brief The id the next new address takes; 0 names no buffer.
                std::uint64_t next_id_ = 1;
        };

    } // namespace Distributed
} // namespace SushiRuntime
