/**************************************************************************/
/* planned_host_access.cpp                                                */
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
 * @file planned_host_access.cpp
 * @brief Implements PlannedHostAccess.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/planned_host_access.hpp>

#include <cstring>
#include <exception>
#include <string>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/coherence_intervals.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the bitmask naming space alone, or 0 past the last space a mask names. */
            std::uint64_t bit_of(MemorySpaceID space) noexcept
            {
                return space.value() < Memory::CoherenceIntervals::K_MAX_MEMORY_SPACES
                           ? std::uint64_t{1} << space.value()
                           : 0;
            }

            /** @brief Returns the error a failed directory call is reported with. */
            Error directory_error(HandleStatus status, const char* what)
            {
                return Error{Memory::error_code_of(status), what};
            }
        } // namespace

        PlannedHostAccess::PlannedHostAccess(Memory::ResidencyDirectory& directory,
                                             const std::vector<const Memory::MemorySpace*>& spaces,
                                             const std::vector<TransferPath*>& paths,
                                             InstanceAllocator& allocator,
                                             MemorySpaceID host) noexcept
            : directory_(&directory), spaces_(&spaces), paths_(&paths), allocator_(&allocator),
              host_(host)
        {

        }

        Result<GlobalHandle> PlannedHostAccess::mint(HandleKind kind, std::size_t bytes,
                                                     Memory::ElementLayout layout)
        {
            if (bytes == 0)
                return GlobalHandle::null();
            const MintOutcome minted = directory_->mint(kind, HandleFlags::None);
            if (minted.status != HandleStatus::Ok)
                return make_error(directory_error(minted.status, "no handle is left for a new planned buffer"));

            // The host space is the recorded home until the partitioner places the buffer.
            const HandleStatus described = directory_->describe(minted.handle, bytes, layout, host_);
            Memory::CoherenceIntervals* const map = directory_->coherence(minted.handle);
            if (described != HandleStatus::Ok || map == nullptr)
            {
                static_cast<void>(directory_->retire(minted.handle));
                return make_error(directory_error(described != HandleStatus::Ok ? described : HandleStatus::StaleHandle,
                                                  "the residency directory refused the planned buffer's entry"));
            }
            // A map that covers the bytes and names no space marks the buffer as held nowhere yet.
            map->reset(bytes, host_);
            map->drop_space(host_);
            return minted.handle;
        }

        Result<void> PlannedHostAccess::write(GlobalHandle handle, std::size_t begin, const void* source,
                                              std::size_t bytes)
        {
            if (bytes == 0)
                return {};
            Result<unsigned char*> host_bytes = host_instance(handle);
            if (!host_bytes)
                return make_error(std::move(host_bytes.error()));
            std::memcpy(*host_bytes + begin, source, bytes);
            // The write leaves the host space the only holder of the range.
            directory_->coherence(handle)->record_write(begin, begin + bytes, host_);
            return {};
        }

        Result<void> PlannedHostAccess::read(GlobalHandle handle, std::size_t begin, void* destination,
                                             std::size_t bytes)
        {
            if (bytes == 0)
                return {};
            Memory::CoherenceIntervals* const map = directory_->coherence(handle);
            if (map == nullptr)
                return make_error(directory_error(HandleStatus::StaleHandle, "the planned buffer is not live"));

            auto* const out = static_cast<unsigned char*>(destination);
            std::memset(out, 0, bytes);
            const std::size_t end = begin + bytes;
            const std::uint64_t host_bit = bit_of(host_);
            const std::vector<Memory::CoherenceInterval> intervals = map->intervals();
            for (const Memory::CoherenceInterval& interval : intervals)
            {
                const std::size_t from = interval.begin > begin ? interval.begin : begin;
                const std::size_t to = interval.end < end ? interval.end : end;
                // Bytes no space holds were never written; they stay zero.
                if (from >= to || interval.valid_spaces == 0)
                    continue;
                Result<unsigned char*> host_bytes = host_instance(handle);
                if (!host_bytes)
                    return make_error(std::move(host_bytes.error()));
                if ((interval.valid_spaces & host_bit) == 0)
                {
                    Result<void> fetched = fetch(handle, from, to, interval.valid_spaces, *host_bytes);
                    if (!fetched)
                        return fetched;
                }
                std::memcpy(out + (from - begin), *host_bytes + from, to - from);
            }
            return {};
        }

        void PlannedHostAccess::release(GlobalHandle handle) noexcept
        {
            const Memory::EntryView entry = directory_->inspect(handle);
            if (entry.status == HandleStatus::Ok)
                for (std::uint32_t space = 0; space < Memory::ResidencyDirectory::K_MAX_MEMORY_SPACES; ++space)
                {
                    if ((entry.valid_spaces & (std::uint64_t{1} << space)) == 0)
                        continue;
                    const ResolveOutcome instance = directory_->resolve(handle, MemorySpaceID(space));
                    static_cast<void>(directory_->drop_instance(handle, MemorySpaceID(space)));
                    if (instance.status == HandleStatus::Ok)
                        allocator_->release(MemorySpaceID(space), instance.address);
                }
            static_cast<void>(directory_->retire(handle));
        }

        Result<unsigned char*> PlannedHostAccess::host_instance(GlobalHandle handle)
        {
            const ResolveOutcome held = directory_->resolve(handle, host_);
            if (held.status == HandleStatus::Ok)
                return static_cast<unsigned char*>(held.address);
            if (held.status != HandleStatus::NotResident)
                return make_error(directory_error(held.status, "the planned buffer is not live"));

            const Memory::EntryView entry = directory_->inspect(handle);
            if (entry.status != HandleStatus::Ok)
                return make_error(directory_error(entry.status, "the planned buffer is not live"));
            void* const address = allocator_->allocate(host_, entry.size, entry.layout.alignment);
            if (address == nullptr)
                return make_error(Error{ErrorCode::AllocationFailed,
                                        "the host space returned no memory for " + std::to_string(entry.size) +
                                            " bytes of a planned buffer."});
            const HandleStatus set = directory_->set_instance(handle, host_, address, allocator_->kind_of(host_));
            if (set != HandleStatus::Ok)
            {
                allocator_->release(host_, address);
                return make_error(directory_error(set, "the residency directory refused the host instance"));
            }
            return static_cast<unsigned char*>(address);
        }

        Result<void> PlannedHostAccess::fetch(GlobalHandle handle, std::size_t begin, std::size_t end,
                                              std::uint64_t valid_spaces, unsigned char* host_bytes)
        {
            const Memory::MemorySpace* const host = space_with(host_);
            for (const Memory::MemorySpace* source : *spaces_)
            {
                if (host == nullptr || (valid_spaces & bit_of(source->id())) == 0)
                    continue;
                const ResolveOutcome from = directory_->resolve(handle, source->id());
                if (from.status != HandleStatus::Ok)
                    continue;
                for (TransferPath* path : *paths_)
                {
                    if (!path->connects(*source, *host))
                        continue;
                    const TransferRequest request{
                        source->id(), host_, static_cast<const unsigned char*>(from.address) + begin,
                        host_bytes + begin, end - begin};
                    try
                    {
                        path->copy(request, {}).wait();
                    }
                    catch (const std::exception& failure)
                    {
                        return make_error(Error{ErrorCode::KernelFailed,
                                                std::string("the copy of a planned buffer to the host failed: ") +
                                                    failure.what()});
                    }
                    directory_->coherence(handle)->record_copy(begin, end, host_);
                    return {};
                }
            }
            return make_error(Error{ErrorCode::Unsupported,
                                    "no transfer path reaches the host space from a space that holds bytes [" +
                                        std::to_string(begin) + ", " + std::to_string(end) +
                                        ") of a planned buffer."});
        }

        const Memory::MemorySpace* PlannedHostAccess::space_with(MemorySpaceID id) const noexcept
        {
            for (const Memory::MemorySpace* space : *spaces_)
                if (space->id() == id)
                    return space;
            return nullptr;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
