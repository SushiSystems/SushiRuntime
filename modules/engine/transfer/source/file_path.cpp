/**************************************************************************/
/* file_path.cpp                                                          */
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
 * @file file_path.cpp
 * @brief Implements FilePath.
 * @author Mustafa Garip
 */

#include <SushiRuntime/transfer/file_path.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Reads bytes from file into destination, or returns an error naming path. */
            [[nodiscard]] Result<void> read_exactly(std::ifstream& file, void* destination, std::size_t bytes,
                                                    const std::string& path)
            {
                file.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
                if (static_cast<std::size_t>(file.gcount()) != bytes)
                    return make_error(Error{ErrorCode::InvalidState, "file path: " + path +
                                                                         " ends before the range of its buffer does."});
                return {};
            }
        } // namespace

        FilePath::FilePath(const SpaceQueues& queues, const StagingRings& rings, TransferObserver* observer) noexcept
            : queues_(&queues), rings_(&rings), observer_(observer)
        {

        }

        void FilePath::add(const Memory::FileMemorySpace& space)
        {
            const auto at = std::lower_bound(spaces_.begin(), spaces_.end(), space.id(),
                                             [](const Memory::FileMemorySpace* held, MemorySpaceID id)
                                             {
                                                 return held->id() < id;
                                             });
            if (at != spaces_.end() && (*at)->id() == space.id())
                *at = &space;
            else
                spaces_.insert(at, &space);
        }

        void FilePath::remove(MemorySpaceID id) noexcept
        {
            const auto at = std::lower_bound(spaces_.begin(), spaces_.end(), id,
                                             [](const Memory::FileMemorySpace* held, MemorySpaceID wanted)
                                             {
                                                 return held->id() < wanted;
                                             });
            if (at != spaces_.end() && (*at)->id() == id)
                spaces_.erase(at);
        }

        std::string_view FilePath::name() const noexcept
        {
            return K_NAME;
        }

        TransferPathID FilePath::id() const noexcept
        {
            return K_ID;
        }

        bool FilePath::connects(const Memory::MemorySpace& source, const Memory::MemorySpace& destination) const
        {
            return space_with(source.id()) == &source && !destination.is_file_backed();
        }

        Result<sycl::event> FilePath::copy(const TransferRequest& request,
                                           const std::vector<sycl::event>& dependencies)
        {
            const Memory::FileMemorySpace* const space = space_with(request.source);
            if (space == nullptr)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "file path: the source space is not a file space of this path."});
            const std::string& path = space->range().path;
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return make_error(Error{ErrorCode::InvalidState, "file path: cannot open " + path + "."});
            file.seekg(static_cast<std::streamoff>(space->file_offset(request.from)));
            if (!file)
                return make_error(Error{ErrorCode::InvalidState, "file path: cannot seek in " + path + "."});

            auto* const to = static_cast<std::byte*>(request.to);
            const std::size_t slot_bytes = rings_->shape().slot_bytes;
            sycl::queue* const to_queue = queues_->queue_of(request.destination);
            sycl::event done;
            if (to_queue == nullptr)
            {
                const Result<void> waited = Internal::SYCL::translate_exceptions([&dependencies]()
                {
                    sycl::event::wait(dependencies);
                });
                if (!waited)
                    return make_error(Error{waited.error()});
                for (std::size_t offset = 0; offset < request.bytes; offset += slot_bytes)
                {
                    const std::size_t chunk = std::min(slot_bytes, request.bytes - offset);
                    const Result<void> read = read_exactly(file, to + offset, chunk, path);
                    if (!read)
                        return make_error(Error{read.error()});
                }
            }
            else
            {
                StagingRing* const ring = rings_->ring_of(to_queue->get_context());
                if (ring == nullptr)
                    return make_error(Error{ErrorCode::InvalidArgument,
                                            "file path: the destination's context has no staging ring."});
                std::vector<sycl::event> after_previous_chunk = dependencies;
                for (std::size_t offset = 0; offset < request.bytes; offset += slot_bytes)
                {
                    const std::size_t chunk = std::min(slot_bytes, request.bytes - offset);
                    const Result<StagingSlot> acquired = ring->acquire();
                    if (!acquired)
                        return make_error(Error{acquired.error()});
                    const StagingSlot slot = acquired.value();
                    // The host writes the slot, so it waits here for the slot's last reader.
                    Result<void> staged = Internal::SYCL::translate_exceptions([&slot]()
                    {
                        sycl::event previous = slot.previous_use;
                        previous.wait();
                    });
                    if (staged)
                        staged = read_exactly(file, slot.data, chunk, path);
                    if (staged)
                    {
                        staged = Internal::SYCL::translate_exceptions([&]()
                        {
                            done = to_queue->memcpy(to + offset, slot.data, chunk, after_previous_chunk);
                        });
                    }
                    if (!staged)
                    {
                        ring->retire(slot, slot.previous_use);
                        return make_error(Error{staged.error()});
                    }
                    ring->retire(slot, done);
                    after_previous_chunk.assign(1, done);
                }
            }
            if (observer_ != nullptr)
                observer_->copied(TransferRecord{name(), request.source, request.destination, request.bytes, K_ID});
            return done;
        }

        const Memory::FileMemorySpace* FilePath::space_with(MemorySpaceID id) const noexcept
        {
            const auto at = std::lower_bound(spaces_.begin(), spaces_.end(), id,
                                             [](const Memory::FileMemorySpace* held, MemorySpaceID wanted)
                                             {
                                                 return held->id() < wanted;
                                             });
            return at != spaces_.end() && (*at)->id() == id ? *at : nullptr;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
