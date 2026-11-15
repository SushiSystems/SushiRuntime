/**************************************************************************/
/* host_write_table.cpp                                                   */
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
 * @file host_write_table.cpp
 * @brief Implements HostWriteTable.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/host_write_table.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        HostWriteTable::HostWriteTable(const ResidencyView& residency)
            : residency_(residency), chunks_(new std::atomic<Chunk*>[K_MAX_CHUNKS])
        {
            for (std::uint32_t i = 0; i < K_MAX_CHUNKS; ++i)
                chunks_[i].store(nullptr, std::memory_order_relaxed);
        }

        HostWriteTable::~HostWriteTable()
        {
            for (std::uint32_t i = 0; i < K_MAX_CHUNKS; ++i)
                delete chunks_[i].load(std::memory_order_relaxed);
        }

        HostWriteTable::Mark* HostWriteTable::find_mark(std::uint32_t slot) const noexcept
        {
            const std::uint32_t chunk_index = slot / K_CHUNK_MARKS;
            if (chunk_index >= K_MAX_CHUNKS)
                return nullptr;
            Chunk* const chunk = chunks_[chunk_index].load(std::memory_order_acquire);
            if (!chunk)
                return nullptr;
            return &chunk->marks[slot % K_CHUNK_MARKS];
        }

        HandleStatus HostWriteTable::record_host_write(GlobalHandle handle)
        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            const HandleStatus status = residency_.inspect(handle).status;
            if (status != HandleStatus::Ok)
                return status;

            const std::uint32_t chunk_index = handle.slot() / K_CHUNK_MARKS;
            if (chunk_index >= K_MAX_CHUNKS)
                return HandleStatus::SpaceExhausted;
            if (!chunks_[chunk_index].load(std::memory_order_relaxed))
                chunks_[chunk_index].store(new Chunk(), std::memory_order_release);

            Mark* const mark = find_mark(handle.slot());
            const std::uint64_t next = generation_.load(std::memory_order_relaxed) + 1;
            if (mark->handle_generation.load(std::memory_order_relaxed) != handle.generation())
            {
                // The old handle's mark is cleared before the slot names the new handle.
                mark->written.store(0, std::memory_order_release);
                mark->handle_generation.store(handle.generation(), std::memory_order_release);
            }
            // The mark is stored first, so a reader that sees the new generation sees the mark.
            mark->written.store(next, std::memory_order_release);
            generation_.store(next, std::memory_order_release);
            return HandleStatus::Ok;
        }

        std::uint64_t HostWriteTable::host_write_generation() const noexcept
        {
            return generation_.load(std::memory_order_acquire);
        }

        std::uint64_t HostWriteTable::host_write_of(GlobalHandle handle) const noexcept
        {
            if (residency_.inspect(handle).status != HandleStatus::Ok)
                return 0;
            const Mark* const mark = find_mark(handle.slot());
            if (!mark)
                return 0;
            if (mark->handle_generation.load(std::memory_order_acquire) != handle.generation())
                return 0;
            const std::uint64_t written = mark->written.load(std::memory_order_acquire);
            // A write for a newer handle of the slot may have replaced the mark between the loads.
            if (mark->handle_generation.load(std::memory_order_acquire) != handle.generation())
                return 0;
            return written;
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
