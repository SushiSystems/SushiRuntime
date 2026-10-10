/**************************************************************************/
/* worker_buffer_cache.hpp                                                */
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
 * @file worker_buffer_cache.hpp
 * @brief Declares WorkerBufferCache, the worker's copies of the master's shared buffers.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>

#include "wire_messages.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Supplies the memory the cache keeps buffers in.
         */
        struct CacheAllocator
        {
            /** @brief Returns host-readable device memory of the size, or null; never throws. */
            std::function<void*(std::size_t size_bytes)> allocate;

            /** @brief Frees what allocate() returned. */
            std::function<void(void* address)> release;
        };

        /**
         * @brief Names what the cache answers when a task asks for a buffer.
         */
        enum class CacheStatus : std::uint8_t
        {
            Ready,            /**< The view is filled and the buffer is pinned once more. */
            NeedsPull,        /**< A transfer was opened; send BUFFER_REQUEST, then wait_input(). */
            AllocationFailed, /**< The budget or the allocator has no room; nothing is pinned. */
            TimedOut,         /**< No chunk arrived for the idle timeout; transfer discarded. */
            Aborted,          /**< The master sent BUFFER_ABORT, or a chunk left a gap. */
            Cancelled,        /**< clear() ran while the caller waited. */
        };

        /**
         * @brief Holds one allocation per buffer id, assembles pulled chunks, and evicts.
         *
         * A pinned buffer is never freed or evicted; every Ready answer is one pin and needs
         * one unpin(). Under the byte budget the least recently used unpinned buffer goes
         * first. The cache owns one mutex and a condition, and calls only its allocator. The
         * exec thread asks and waits; the reader thread delivers chunks, aborts and releases.
         */
        class SUSHIRUNTIME_API WorkerBufferCache final
        {
            public:
                /**
                 * @brief Builds an empty cache.
                 * @param allocator Both callables set; called with the cache's mutex held.
                 * @param budget_bytes Most bytes held at once; already resolved, never 0.
                 */
                WorkerBufferCache(CacheAllocator allocator, std::uint64_t budget_bytes);

                /** @brief Frees every buffer; no pin may be outstanding. */
                ~WorkerBufferCache();

                WorkerBufferCache(const WorkerBufferCache&) = delete;
                WorkerBufferCache& operator=(const WorkerBufferCache&) = delete;
                WorkerBufferCache(WorkerBufferCache&&) = delete;
                WorkerBufferCache& operator=(WorkerBufferCache&&) = delete;

                /**
                 * @brief Asks for an input at exactly the handle's version.
                 * @return Ready on a hit; NeedsPull after allocating room for the bytes and
                 *         opening a transfer; AllocationFailed.
                 */
                [[nodiscard]] CacheStatus begin_input(const BufferHandle& handle, BufferView& view);

                /**
                 * @brief Waits for the transfer begin_input() opened to complete.
                 * @param idle_timeout Restarts on each chunk of this buffer; not positive
                 *        waits without a limit.
                 * @return Ready, TimedOut, Aborted or Cancelled.
                 */
                [[nodiscard]] CacheStatus wait_input(const BufferHandle& handle,
                                                     std::chrono::milliseconds idle_timeout,
                                                     BufferView& view);

                /**
                 * @brief Copies one pull-reply chunk into its buffer; called per BUFFER_CHUNK.
                 *
                 * Accepts offset 0 or the next expected offset of an open transfer at the
                 * chunk's version; anything else discards the transfer and wakes its waiter
                 * with Aborted. The chunk with the last-chunk flag completes it.
                 */
                void accept_chunk(const Wire::BufferChunkMessage& chunk, bool last_chunk);

                /** @brief Ends an open transfer and wakes its waiter with Aborted. */
                void abort_transfer(GlobalBufferID id, std::uint32_t version);

                /**
                 * @brief Pins memory for an output, reusing the buffer's allocation when the
                 *        size matches.
                 * @return Ready or AllocationFailed; it never throws.
                 */
                [[nodiscard]] CacheStatus pin_output(const BufferHandle& handle, BufferView& view);

                /** @brief Records that a pinned output holds the handle's version from here on. */
                void commit_output(const BufferHandle& handle);

                /** @brief Drops one pin; a buffer released or cleared meanwhile is freed. */
                void unpin(GlobalBufferID id) noexcept;

                /** @brief Evicts a buffer for BUFFER_RELEASE; a pinned one goes when unpinned. */
                void release(GlobalBufferID id);

                /**
                 * @brief Empties the cache for a new master session and wakes every waiter
                 *        with Cancelled; pinned buffers go at their last unpin.
                 */
                void clear();

                /** @brief Returns the bytes held; reads an atomic. */
                [[nodiscard]] std::uint64_t bytes() const noexcept;

                /** @brief Returns the byte budget. */
                [[nodiscard]] std::uint64_t budget() const noexcept;

                /** @brief Returns the buffers evicted for the budget or released; an atomic. */
                [[nodiscard]] std::uint64_t evictions() const noexcept;

            private:
                /// @brief Guards the entries and the open transfers.
                mutable std::mutex mutex_;

                /// @brief Wakes wait_input() on a chunk, an abort and clear().
                std::condition_variable progress_;

                /// @brief Supplies and frees the memory.
                CacheAllocator allocator_;

                /// @brief Most bytes held at once.
                std::uint64_t budget_bytes_;

                /// @brief Bytes held.
                std::atomic<std::uint64_t> bytes_{0};

                /// @brief Buffers evicted or released.
                std::atomic<std::uint64_t> evictions_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
