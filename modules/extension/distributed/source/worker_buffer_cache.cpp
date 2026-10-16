/**************************************************************************/
/* worker_buffer_cache.cpp                                                */
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
 * @file worker_buffer_cache.cpp
 * @brief Implements WorkerBufferCache: chunk assembly, pins, the byte budget and eviction.
 * @author Mustafa Garip
 */

#include "worker_buffer_cache.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/distributed/distributed_log_category.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        void WorkerBufferCache::touch(Entry& entry)
        {
            recency_.splice(recency_.end(), recency_, entry.recency);
        }

        void WorkerBufferCache::free_memory(Entry& entry)
        {
            if (entry.data != nullptr)
                allocator_.release(entry.data);
            bytes_.fetch_sub(entry.size_bytes, std::memory_order_relaxed);
            entry.data = nullptr;
        }

        void WorkerBufferCache::retire(Live::iterator position)
        {
            Entry& entry = position->second;
            recency_.erase(entry.recency);
            if (entry.pins > 0)
            {
                entry.detached = true;
                detached_.push_back(std::move(entry));
            }
            else
            {
                free_memory(entry);
            }
            live_.erase(position);
        }

        bool WorkerBufferCache::evict_one()
        {
            for (const std::uint64_t id : recency_)
            {
                const Live::iterator position = live_.find(id);
                if (position->second.pins > 0)
                    continue;

                retire(position);
                evictions_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            return false;
        }

        WorkerBufferCache::Entry* WorkerBufferCache::create(const BufferHandle& handle)
        {
            const std::uint64_t size = handle.size_bytes;
            if (size > budget_bytes_ || size > std::numeric_limits<std::size_t>::max())
                return nullptr;

            while (bytes_.load(std::memory_order_relaxed) + size > budget_bytes_)
            {
                if (!evict_one())
                    return nullptr;
            }

            void* data = nullptr;
            while (size > 0 && data == nullptr)
            {
                data = allocator_.allocate(static_cast<std::size_t>(size));
                if (data == nullptr && !evict_one())
                    return nullptr;
            }

            Entry entry;
            entry.id = handle.id;
            entry.data = data;
            entry.size_bytes = size;
            entry.version = handle.version;
            entry.recency = recency_.insert(recency_.end(), handle.id.value);
            bytes_.fetch_add(size, std::memory_order_relaxed);
            return &live_.insert_or_assign(handle.id.value, std::move(entry)).first->second;
        }

        WorkerBufferCache::Entry* WorkerBufferCache::find_transfer(GlobalBufferID id,
                                                                   std::uint32_t version)
        {
            const Live::iterator position = live_.find(id.value);
            if (position != live_.end() && position->second.version == version &&
                position->second.transfer != Transfer::None)
                return &position->second;

            for (Entry& entry : detached_)
            {
                if (entry.id == id && entry.version == version &&
                    entry.transfer != Transfer::None)
                    return &entry;
            }
            return nullptr;
        }

        WorkerBufferCache::Entry* WorkerBufferCache::find_detached(GlobalBufferID id)
        {
            for (Entry& entry : detached_)
            {
                if (entry.id == id && entry.pins > 0)
                    return &entry;
            }
            return nullptr;
        }

        void WorkerBufferCache::drop_pin(Entry& entry)
        {
            if (entry.pins > 0)
                --entry.pins;
            if (entry.pins > 0)
                return;

            if (entry.detached)
            {
                free_memory(entry);
                detached_.remove_if([&entry](const Entry& candidate)
                                    {
                                        return &candidate == &entry;
                                    });
            }
            else if (!entry.valid && entry.transfer == Transfer::None)
            {
                retire(live_.find(entry.id.value));
            }
        }

        void WorkerBufferCache::fill(const Entry& entry, BufferView& view) noexcept
        {
            view.data = entry.data;
            view.size_bytes = static_cast<std::size_t>(entry.size_bytes);
        }

        WorkerBufferCache::WorkerBufferCache(CacheAllocator allocator, std::uint64_t budget_bytes)
            : allocator_(std::move(allocator)), budget_bytes_(budget_bytes)
        {

        }

        WorkerBufferCache::~WorkerBufferCache()
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, entry] : live_)
                free_memory(entry);
            for (Entry& entry : detached_)
                free_memory(entry);
        }

        CacheStatus WorkerBufferCache::begin_input(const BufferHandle& handle, BufferView& view)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            Entry* entry = nullptr;

            const Live::iterator position = live_.find(handle.id.value);
            if (position != live_.end())
            {
                Entry& found = position->second;
                if (found.size_bytes != handle.size_bytes)
                {
                    retire(position);
                }
                else if (found.valid && found.version == handle.version &&
                         found.transfer == Transfer::None)
                {
                    ++found.pins;
                    touch(found);
                    fill(found, view);
                    return CacheStatus::Ready;
                }
                else
                {
                    entry = &found;
                }
            }

            if (entry == nullptr)
                entry = create(handle);
            if (entry == nullptr)
                return CacheStatus::AllocationFailed;

            // This pin is the waiter's: wait_input() keeps it on Ready and drops it otherwise.
            ++entry->pins;
            entry->valid = false;
            entry->version = handle.version;
            entry->transfer = Transfer::Open;
            entry->next_offset = 0;
            entry->chunks = 0;
            touch(*entry);
            return CacheStatus::NeedsPull;
        }

        CacheStatus WorkerBufferCache::wait_input(const BufferHandle& handle,
                                                  std::chrono::milliseconds idle_timeout,
                                                  BufferView& view)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool bounded = idle_timeout > std::chrono::milliseconds::zero();
            std::uint64_t chunks_seen = 0;
            auto deadline = std::chrono::steady_clock::now() + idle_timeout;

            for (;;)
            {
                Entry* entry = find_transfer(handle.id, handle.version);
                if (entry == nullptr)
                    return CacheStatus::Aborted;

                if (entry->transfer == Transfer::Complete)
                {
                    entry->transfer = Transfer::None;
                    entry->valid = true;
                    fill(*entry, view);
                    return CacheStatus::Ready;
                }

                if (entry->transfer != Transfer::Open)
                {
                    const CacheStatus status = entry->transfer == Transfer::Cancelled
                                                   ? CacheStatus::Cancelled
                                                   : CacheStatus::Aborted;
                    entry->transfer = Transfer::None;
                    drop_pin(*entry);
                    return status;
                }

                if (entry->chunks != chunks_seen)
                {
                    chunks_seen = entry->chunks;
                    deadline = std::chrono::steady_clock::now() + idle_timeout;
                }

                if (!bounded)
                {
                    progress_.wait(lock);
                }
                else if (std::chrono::steady_clock::now() >= deadline)
                {
                    entry->transfer = Transfer::None;
                    drop_pin(*entry);
                    return CacheStatus::TimedOut;
                }
                else
                {
                    progress_.wait_until(lock, deadline);
                }
            }
        }

        void WorkerBufferCache::accept_chunk(const Wire::BufferChunkMessage& chunk,
                                             bool last_chunk)
        {
            if (chunk.purpose != Wire::ChunkPurpose::PullReply)
                return;

            bool discarded = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                const Live::iterator position = live_.find(chunk.handle.id.value);
                if (position == live_.end())
                    return;

                Entry& entry = position->second;
                if (entry.transfer != Transfer::Open ||
                    entry.version != chunk.handle.version)
                    return;

                if (chunk.offset == 0)
                    entry.next_offset = 0;

                const std::uint64_t length = chunk.data.size();
                const bool fits = chunk.handle.size_bytes == entry.size_bytes &&
                                  chunk.offset == entry.next_offset &&
                                  length <= entry.size_bytes - entry.next_offset;
                if (fits)
                {
                    if (length > 0)
                        std::memcpy(static_cast<std::byte*>(entry.data) + chunk.offset,
                                    chunk.data.data(), static_cast<std::size_t>(length));
                    entry.next_offset += length;
                    ++entry.chunks;
                }

                const bool whole = entry.next_offset == entry.size_bytes;
                if (!fits || (last_chunk && !whole))
                {
                    entry.transfer = Transfer::Aborted;
                    discarded = true;
                }
                else if (last_chunk)
                {
                    entry.transfer = Transfer::Complete;
                }
            }

            progress_.notify_all();
            if (discarded)
                SR_LOG_DEBUG(K_DISTRIBUTED_LOG_CATEGORY, "pull of buffer {} version {} discarded: chunk at offset {} with {} "
                             "bytes does not continue the transfer",
                             chunk.handle.id.value, chunk.handle.version, chunk.offset,
                             chunk.data.size());
        }

        void WorkerBufferCache::abort_transfer(GlobalBufferID id, std::uint32_t version)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                const Live::iterator position = live_.find(id.value);
                if (position == live_.end())
                    return;

                Entry& entry = position->second;
                if (entry.transfer != Transfer::Open || entry.version != version)
                    return;

                entry.transfer = Transfer::Aborted;
            }
            progress_.notify_all();
        }

        CacheStatus WorkerBufferCache::pin_output(const BufferHandle& handle, BufferView& view)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            Entry* entry = nullptr;

            const Live::iterator position = live_.find(handle.id.value);
            if (position != live_.end())
            {
                if (position->second.size_bytes == handle.size_bytes)
                    entry = &position->second;
                else
                    retire(position);
            }

            if (entry == nullptr)
                entry = create(handle);
            if (entry == nullptr)
                return CacheStatus::AllocationFailed;

            ++entry->pins;
            touch(*entry);
            fill(*entry, view);
            return CacheStatus::Ready;
        }

        void WorkerBufferCache::commit_output(const BufferHandle& handle)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const Live::iterator position = live_.find(handle.id.value);
            if (position == live_.end())
                return;

            position->second.version = handle.version;
            position->second.valid = true;
            touch(position->second);
        }

        void WorkerBufferCache::unpin(GlobalBufferID id) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);

            // A detached buffer is the older holder of the id, so its pins are returned first.
            if (Entry* detached = find_detached(id))
            {
                drop_pin(*detached);
                return;
            }

            const Live::iterator position = live_.find(id.value);
            if (position != live_.end())
                drop_pin(position->second);
        }

        void WorkerBufferCache::release(GlobalBufferID id)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                const Live::iterator position = live_.find(id.value);
                if (position == live_.end())
                    return;

                if (position->second.transfer != Transfer::None)
                    position->second.transfer = Transfer::Aborted;
                retire(position);
                evictions_.fetch_add(1, std::memory_order_relaxed);
            }
            progress_.notify_all();
        }

        void WorkerBufferCache::clear()
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                while (!live_.empty())
                {
                    const Live::iterator position = live_.begin();
                    if (position->second.transfer != Transfer::None)
                        position->second.transfer = Transfer::Cancelled;
                    retire(position);
                }
            }
            progress_.notify_all();
        }

        std::uint64_t WorkerBufferCache::bytes() const noexcept
        {
            return bytes_.load(std::memory_order_relaxed);
        }

        std::uint64_t WorkerBufferCache::budget() const noexcept
        {
            return budget_bytes_;
        }

        std::uint64_t WorkerBufferCache::evictions() const noexcept
        {
            return evictions_.load(std::memory_order_relaxed);
        }

    } // namespace Distributed
} // namespace SushiRuntime
