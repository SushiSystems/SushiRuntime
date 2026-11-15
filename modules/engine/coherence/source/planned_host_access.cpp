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
#include <optional>
#include <string>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/coherence_intervals.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>

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

            /** @brief Returns whether some space holds a byte of [begin, end) in map. */
            bool holds_any(const Memory::CoherenceIntervals& map, std::size_t begin, std::size_t end) noexcept
            {
                for (const Memory::CoherenceInterval& interval : map.intervals())
                    if (interval.begin < end && interval.end > begin && interval.valid_spaces != 0)
                        return true;
                return false;
            }

            /** @brief Returns the error host access is refused with while a run holds the buffer. */
            Error held_by_run_error()
            {
                return Error{ErrorCode::InvalidState,
                             "a run of a graph that holds the planned buffer is in flight; host access waits for "
                             "the run to finish."};
            }

            /** @brief Returns the error a failed directory call is reported with. */
            Error directory_error(HandleStatus status, const char* what)
            {
                return Error{Memory::error_code_of(status), what};
            }
        } // namespace

        PlannedHostAccess::PlannedHostAccess(const Memory::HandlePorts& ports,
                                             const std::vector<const Memory::MemorySpace*>& spaces,
                                             const std::vector<TransferPath*>& paths,
                                             InstanceAllocator& allocator,
                                             MemorySpaceID host) noexcept
            : ports_(ports), spaces_(&spaces), paths_(&paths), allocator_(&allocator),
              host_(host)
        {

        }

        Result<GlobalHandle> PlannedHostAccess::mint(HandleKind kind, std::size_t bytes,
                                                     Memory::ElementLayout layout)
        {
            if (bytes == 0)
                return GlobalHandle::null();
            const MintOutcome minted = ports_.registry.mint(kind, HandleFlags::None);
            if (minted.status != HandleStatus::Ok)
                return make_error(directory_error(minted.status, "no handle is left for a new planned buffer"));

            // The host space is the recorded home until the partitioner places the buffer.
            const HandleStatus described = ports_.registry.describe(minted.handle, bytes, layout, host_);
            {
                Memory::LockedCoherence map = ports_.residency.coherence(minted.handle);
                if (described == HandleStatus::Ok && map != nullptr)
                {
                    // A map that covers the bytes and names no space marks the buffer as held nowhere yet.
                    map->reset(bytes, host_);
                    map->drop_space(host_);
                    return minted.handle;
                }
            }
            static_cast<void>(ports_.registry.retire(minted.handle));
            return make_error(directory_error(described != HandleStatus::Ok ? described : HandleStatus::StaleHandle,
                                              "the residency directory refused the planned buffer's entry"));
        }

        Result<GlobalHandle> PlannedHostAccess::mint_held(HandleKind kind, std::size_t bytes,
                                                          Memory::ElementLayout layout, MemorySpaceID space,
                                                          void* address)
        {
            Result<GlobalHandle> minted = mint(kind, bytes, layout);
            if (!minted || minted->is_null())
                return minted;
            const HandleStatus set = ports_.residency.set_instance(*minted, space, address, std::nullopt);
            if (set != HandleStatus::Ok)
            {
                static_cast<void>(ports_.registry.retire(*minted));
                return make_error(directory_error(set, "the residency directory refused the held instance"));
            }
            // The held instance is the one valid copy of every byte.
            if (Memory::LockedCoherence map = ports_.residency.coherence(*minted))
                map->reset(bytes, space);
            return minted;
        }

        Result<void> PlannedHostAccess::write(GlobalHandle handle, std::size_t begin, const void* source,
                                              std::size_t bytes)
        {
            if (bytes == 0)
                return {};
            Result<unsigned char*> host_bytes = host_instance(handle);
            if (!host_bytes)
                return make_error(std::move(host_bytes.error()));
            {
                Memory::LockedCoherence map = ports_.residency.coherence(handle);
                if (map == nullptr)
                    return make_error(directory_error(HandleStatus::StaleHandle, "the planned buffer is not live"));
                if (map.held_by_run())
                    return make_error(held_by_run_error());
                std::memcpy(*host_bytes + begin, source, bytes);
                // The write leaves the host space the only holder of the range.
                map->record_write(begin, begin + bytes, host_);
            }
            // Outside the map's lock, which the writer mutex is never taken under; a plan made before is made again.
            static_cast<void>(ports_.host_writes.record_host_write(handle));
            return {};
        }

        Result<void> PlannedHostAccess::read(GlobalHandle handle, std::size_t begin, void* destination,
                                             std::size_t bytes)
        {
            if (bytes == 0)
                return {};
            const std::size_t end = begin + bytes;
            Memory::LockedCoherence map = ports_.residency.coherence(handle);
            if (map == nullptr)
                return make_error(directory_error(HandleStatus::StaleHandle, "the planned buffer is not live"));
            if (map.held_by_run())
                return make_error(held_by_run_error());

            auto* const out = static_cast<unsigned char*>(destination);
            std::memset(out, 0, bytes);
            if (!holds_any(*map, begin, end))
                return {};
            // Making the host instance takes the writer mutex, never taken under the map's lock.
            map = Memory::LockedCoherence{};
            Result<unsigned char*> host_bytes = host_instance(handle);
            if (!host_bytes)
                return make_error(std::move(host_bytes.error()));
            map = ports_.residency.coherence(handle);
            if (map == nullptr)
                return make_error(directory_error(HandleStatus::StaleHandle, "the planned buffer is not live"));
            if (map.held_by_run())
                return make_error(held_by_run_error());
            const std::uint64_t host_bit = bit_of(host_);
            const std::vector<Memory::CoherenceInterval> intervals = map->intervals();
            for (const Memory::CoherenceInterval& interval : intervals)
            {
                const std::size_t from = interval.begin > begin ? interval.begin : begin;
                const std::size_t to = interval.end < end ? interval.end : end;
                // Bytes no space holds were never written; they stay zero.
                if (from >= to || interval.valid_spaces == 0)
                    continue;
                if ((interval.valid_spaces & host_bit) == 0)
                {
                    Result<void> fetched = fetch(handle, from, to, interval.valid_spaces, *host_bytes);
                    if (!fetched)
                        return fetched;
                    map->record_copy(from, to, host_);
                }
                std::memcpy(out + (from - begin), *host_bytes + from, to - from);
            }
            return {};
        }

        std::vector<MemorySpaceID> PlannedHostAccess::held_spaces(GlobalHandle handle) const
        {
            std::vector<MemorySpaceID> held;
            const Memory::EntryView entry = ports_.residency.inspect(handle);
            if (entry.status != HandleStatus::Ok)
                return held;
            for (std::uint32_t space = 0; space < Memory::CoherenceIntervals::K_MAX_MEMORY_SPACES; ++space)
                if ((entry.valid_spaces & (std::uint64_t{1} << space)) != 0 &&
                    !ports_.residency.instance_kind(handle, MemorySpaceID(space)).has_value())
                    held.push_back(MemorySpaceID(space));
            return held;
        }

        void PlannedHostAccess::release(GlobalHandle handle) noexcept
        {
            const Memory::EntryView entry = ports_.residency.inspect(handle);
            if (entry.status == HandleStatus::Ok)
                for (std::uint32_t space = 0; space < Memory::CoherenceIntervals::K_MAX_MEMORY_SPACES; ++space)
                {
                    if ((entry.valid_spaces & (std::uint64_t{1} << space)) == 0)
                        continue;
                    const ResolveOutcome instance = ports_.residency.resolve(handle, MemorySpaceID(space));
                    // An instance with no kind was not allocated here, so it is not freed here.
                    const bool allocated = ports_.residency.instance_kind(handle, MemorySpaceID(space)).has_value();
                    static_cast<void>(ports_.residency.drop_instance(handle, MemorySpaceID(space)));
                    if (instance.status == HandleStatus::Ok && allocated)
                        allocator_->release(MemorySpaceID(space), instance.address);
                }
            static_cast<void>(ports_.registry.retire(handle));
        }

        Result<unsigned char*> PlannedHostAccess::host_instance(GlobalHandle handle)
        {
            const ResolveOutcome held = ports_.residency.resolve(handle, host_);
            if (held.status == HandleStatus::Ok)
                return static_cast<unsigned char*>(held.address);
            if (held.status != HandleStatus::NotResident)
                return make_error(directory_error(held.status, "the planned buffer is not live"));

            const Memory::EntryView entry = ports_.residency.inspect(handle);
            if (entry.status != HandleStatus::Ok)
                return make_error(directory_error(entry.status, "the planned buffer is not live"));
            void* const address = allocator_->allocate(host_, entry.size, entry.layout.alignment);
            if (address == nullptr)
                return make_error(Error{ErrorCode::AllocationFailed,
                                        "the host space returned no memory for " + std::to_string(entry.size) +
                                            " bytes of a planned buffer."});
            const HandleStatus set = ports_.residency.set_instance(handle, host_, address, allocator_->kind_of(host_));
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
                const ResolveOutcome from = ports_.residency.resolve(handle, source->id());
                if (from.status != HandleStatus::Ok)
                    continue;
                for (TransferPath* path : *paths_)
                {
                    if (!path->connects(*source, *host))
                        continue;
                    const TransferRequest request{
                        source->id(), host_, static_cast<const unsigned char*>(from.address) + begin,
                        host_bytes + begin, end - begin};
                    Result<sycl::event> copied = path->copy(request, {});
                    Result<void> finished = copied ? Internal::SYCL::translate_exceptions([&copied]()
                                                     {
                                                         copied.value().wait();
                                                     })
                                                   : Result<void>(make_error(Error{copied.error()}));
                    if (!finished)
                        return make_error(Error{finished.error().code,
                                                "the copy of a planned buffer to the host failed: " +
                                                    finished.error().message});
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
