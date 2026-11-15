/**************************************************************************/
/* residency_directory.cpp                                                */
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
 * @file residency_directory.cpp
 * @brief Implements ResidencyDirectory and the mapping from HandleStatus to ErrorCode.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/residency_directory.hpp>

#include <thread>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Returns the stored form of a kind: the kind plus one, or 0 for nothing. */
            [[nodiscard]] std::uint8_t encode_kind(std::optional<USMKind> kind) noexcept
            {
                return kind ? static_cast<std::uint8_t>(static_cast<std::uint8_t>(*kind) + 1U) : 0U;
            }

            /** @brief Returns the kind a stored value encodes, or nothing for 0. */
            [[nodiscard]] std::optional<USMKind> decode_kind(std::uint8_t stored) noexcept
            {
                if (stored == 0)
                    return std::nullopt;
                return static_cast<USMKind>(stored - 1U);
            }
        } // namespace

        ResidencyDirectory::ResidencyDirectory(std::uint32_t universe)
            : universe_(universe), chunks_(new std::atomic<Chunk*>[K_MAX_CHUNKS])
        {
            for (std::uint32_t i = 0; i < K_MAX_CHUNKS; ++i)
                chunks_[i].store(nullptr, std::memory_order_relaxed);
        }

        ResidencyDirectory::~ResidencyDirectory()
        {
            for (std::uint32_t i = 0; i < K_MAX_CHUNKS; ++i)
                delete chunks_[i].load(std::memory_order_relaxed);
        }

        ResidencyDirectory::Entry* ResidencyDirectory::find_entry(std::uint32_t slot) const noexcept
        {
            const std::uint32_t chunk_index = slot / K_CHUNK_ENTRIES;
            if (chunk_index >= K_MAX_CHUNKS)
                return nullptr;
            Chunk* const chunk = chunks_[chunk_index].load(std::memory_order_acquire);
            if (!chunk)
                return nullptr;
            return &chunk->entries[slot % K_CHUNK_ENTRIES];
        }

        const void* ResidencyDirectory::entry_address(std::uint32_t slot) const noexcept
        {
            return find_entry(slot);
        }

        ResidencyDirectory::Entry* ResidencyDirectory::live_entry(GlobalHandle handle,
                                                                  HandleStatus& status) const noexcept
        {
            if (handle.is_null())
            {
                status = HandleStatus::NullHandle;
                return nullptr;
            }
            if (handle.universe() != universe_)
            {
                status = HandleStatus::ForeignUniverse;
                return nullptr;
            }
            Entry* const entry = find_entry(handle.slot());
            if (!entry || entry->generation.load(std::memory_order_acquire) != handle.generation() ||
                !entry->live.load(std::memory_order_acquire))
            {
                status = HandleStatus::StaleHandle;
                return nullptr;
            }
            status = HandleStatus::Ok;
            return entry;
        }

        bool ResidencyDirectory::take_slot(std::uint32_t& slot)
        {
            if (!free_slots_.empty())
            {
                slot = free_slots_.top();
                free_slots_.pop();
                return true;
            }
            if (next_fresh_slot_ / K_CHUNK_ENTRIES >= K_MAX_CHUNKS)
                return false;
            const std::uint32_t chunk_index = next_fresh_slot_ / K_CHUNK_ENTRIES;
            if (!chunks_[chunk_index].load(std::memory_order_relaxed))
                chunks_[chunk_index].store(new Chunk(), std::memory_order_release);
            slot = next_fresh_slot_++;
            return true;
        }

        MintOutcome ResidencyDirectory::mint(HandleKind kind, HandleFlags flags)
        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            std::uint32_t slot = 0;
            if (!take_slot(slot))
                return MintOutcome{GlobalHandle::null(), HandleStatus::SpaceExhausted};

            Entry* const entry = find_entry(slot);
            const std::uint32_t generation = entry->generation.load(std::memory_order_relaxed);
            entry->live.store(true, std::memory_order_release);
            live_count_.fetch_add(1, std::memory_order_relaxed);
            return MintOutcome{GlobalHandle(universe_, 0, kind, flags, generation, slot),
                               HandleStatus::Ok};
        }

        HandleStatus ResidencyDirectory::retire(GlobalHandle handle)
        {
            {
                std::lock_guard<std::mutex> lock(writer_mutex_);
                HandleStatus status = HandleStatus::Ok;
                Entry* const entry = live_entry(handle, status);
                if (!entry)
                    return status;

                const std::uint32_t next_generation = handle.generation() + 1;
                entry->live.store(false, std::memory_order_relaxed);
                live_count_.fetch_sub(1, std::memory_order_relaxed);
                entry->generation.store(next_generation, std::memory_order_relaxed);
                // Readers that see the cleared fields below also see the new generation.
                std::atomic_thread_fence(std::memory_order_release);

                entry->valid_spaces.store(0, std::memory_order_relaxed);
                for (std::atomic<void*>& instance : entry->instances)
                    instance.store(nullptr, std::memory_order_relaxed);
                for (std::atomic<std::uint8_t>& kind : entry->instance_kinds)
                    kind.store(0, std::memory_order_relaxed);
                entry->size.store(0, std::memory_order_relaxed);
                entry->element_size.store(1, std::memory_order_relaxed);
                entry->element_alignment.store(1, std::memory_order_relaxed);
                entry->home.store(0, std::memory_order_relaxed);
                entry->coherence.clear();
                entry->host_write.store(0, std::memory_order_relaxed);

                // A slot whose generation wrapped to 0 is never reused, so no old handle matches it.
                if (next_generation != 0)
                    free_slots_.push(handle.slot());
            }

            // Counted before the load: zero read after a store means no call holds the old one.
            observer_calls_in_flight_.fetch_add(1, std::memory_order_seq_cst);
            if (RetireObserver* const observer = retire_observer_.load(std::memory_order_seq_cst))
                observer->on_retire(handle);
            observer_calls_in_flight_.fetch_sub(1, std::memory_order_seq_cst);
            return HandleStatus::Ok;
        }

        void ResidencyDirectory::set_retire_observer(RetireObserver* observer) noexcept
        {
            retire_observer_.store(observer, std::memory_order_seq_cst);
            // Stored before the count is read: a retire() counted later loads the new observer.
            while (observer_calls_in_flight_.load(std::memory_order_seq_cst) != 0)
                std::this_thread::yield();
        }

        ResolveOutcome ResidencyDirectory::resolve(GlobalHandle handle, MemorySpaceID space) const
        {
            HandleStatus status = HandleStatus::Ok;
            const Entry* const entry = live_entry(handle, status);
            if (!entry)
                return ResolveOutcome{nullptr, status};
            if (space.value() >= K_MAX_MEMORY_SPACES)
                return ResolveOutcome{nullptr, HandleStatus::NotResident};

            const std::uint64_t mask = entry->valid_spaces.load(std::memory_order_acquire);
            void* const address = (mask >> space.value()) & 1U
                                      ? entry->instances[space.value()].load(std::memory_order_relaxed)
                                      : nullptr;

            // A retire that raced the loads above shows here as a new generation.
            std::atomic_thread_fence(std::memory_order_acquire);
            if (entry->generation.load(std::memory_order_relaxed) != handle.generation() ||
                !entry->live.load(std::memory_order_relaxed))
                return ResolveOutcome{nullptr, HandleStatus::StaleHandle};
            if (!address)
                return ResolveOutcome{nullptr, HandleStatus::NotResident};
            return ResolveOutcome{address, HandleStatus::Ok};
        }

        bool ResidencyDirectory::is_stale(GlobalHandle handle) const
        {
            HandleStatus status = HandleStatus::Ok;
            (void)live_entry(handle, status);
            return status == HandleStatus::StaleHandle;
        }

        HandleStatus ResidencyDirectory::describe(GlobalHandle handle, std::size_t size,
                                                  ElementLayout layout, MemorySpaceID home)
        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            HandleStatus status = HandleStatus::Ok;
            Entry* const entry = live_entry(handle, status);
            if (!entry)
                return status;
            entry->size.store(size, std::memory_order_relaxed);
            entry->element_size.store(layout.size, std::memory_order_relaxed);
            entry->element_alignment.store(layout.alignment, std::memory_order_relaxed);
            entry->home.store(home.value(), std::memory_order_release);
            return HandleStatus::Ok;
        }

        HandleStatus ResidencyDirectory::set_instance(GlobalHandle handle, MemorySpaceID space,
                                                      void* address, std::optional<USMKind> kind)
        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            HandleStatus status = HandleStatus::Ok;
            Entry* const entry = live_entry(handle, status);
            if (!entry)
                return status;
            if (space.value() >= K_MAX_MEMORY_SPACES || !address)
                return HandleStatus::NotResident;
            entry->instances[space.value()].store(address, std::memory_order_relaxed);
            entry->instance_kinds[space.value()].store(encode_kind(kind), std::memory_order_relaxed);
            const std::uint64_t bit = std::uint64_t{1} << space.value();
            entry->valid_spaces.fetch_or(bit, std::memory_order_release);
            return HandleStatus::Ok;
        }

        HandleStatus ResidencyDirectory::drop_instance(GlobalHandle handle, MemorySpaceID space)
        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            HandleStatus status = HandleStatus::Ok;
            Entry* const entry = live_entry(handle, status);
            if (!entry)
                return status;
            if (space.value() >= K_MAX_MEMORY_SPACES)
                return HandleStatus::NotResident;
            const std::uint64_t bit = std::uint64_t{1} << space.value();
            entry->valid_spaces.fetch_and(~bit, std::memory_order_release);
            entry->instances[space.value()].store(nullptr, std::memory_order_relaxed);
            entry->instance_kinds[space.value()].store(0, std::memory_order_relaxed);
            return HandleStatus::Ok;
        }

        std::optional<USMKind> ResidencyDirectory::instance_kind(GlobalHandle handle,
                                                                 MemorySpaceID space) const
        {
            HandleStatus status = HandleStatus::Ok;
            const Entry* const entry = live_entry(handle, status);
            if (!entry || space.value() >= K_MAX_MEMORY_SPACES)
                return std::nullopt;
            const std::uint64_t mask = entry->valid_spaces.load(std::memory_order_acquire);
            const std::uint8_t stored =
                (mask >> space.value()) & 1U
                    ? entry->instance_kinds[space.value()].load(std::memory_order_relaxed)
                    : std::uint8_t{0};

            // A retire that raced the loads above shows here as a new generation.
            std::atomic_thread_fence(std::memory_order_acquire);
            if (entry->generation.load(std::memory_order_relaxed) != handle.generation() ||
                !entry->live.load(std::memory_order_relaxed))
                return std::nullopt;
            return decode_kind(stored);
        }

        EntryView ResidencyDirectory::inspect(GlobalHandle handle) const
        {
            EntryView view;
            const Entry* const entry = live_entry(handle, view.status);
            if (!entry)
                return view;
            view.home = MemorySpaceID(entry->home.load(std::memory_order_acquire));
            view.size = entry->size.load(std::memory_order_relaxed);
            view.layout.size = entry->element_size.load(std::memory_order_relaxed);
            view.layout.alignment = entry->element_alignment.load(std::memory_order_relaxed);
            view.valid_spaces = entry->valid_spaces.load(std::memory_order_acquire);
            if (view.home.value() < K_MAX_MEMORY_SPACES && ((view.valid_spaces >> view.home.value()) & 1U))
                view.home_kind = decode_kind(
                    entry->instance_kinds[view.home.value()].load(std::memory_order_relaxed));

            std::atomic_thread_fence(std::memory_order_acquire);
            if (entry->generation.load(std::memory_order_relaxed) != handle.generation() ||
                !entry->live.load(std::memory_order_relaxed))
                return EntryView{0, ElementLayout{}, MemorySpaceID(), 0, HandleStatus::StaleHandle,
                                 std::nullopt};
            return view;
        }

        CoherenceIntervals* ResidencyDirectory::coherence(GlobalHandle handle) noexcept
        {
            HandleStatus status = HandleStatus::Ok;
            Entry* const entry = live_entry(handle, status);
            return entry ? &entry->coherence : nullptr;
        }

        const CoherenceIntervals* ResidencyDirectory::coherence(GlobalHandle handle) const noexcept
        {
            HandleStatus status = HandleStatus::Ok;
            const Entry* const entry = live_entry(handle, status);
            return entry ? &entry->coherence : nullptr;
        }

        std::size_t ResidencyDirectory::retire_coherence_history()
        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            std::size_t intervals = 0;
            for (std::uint32_t slot = 0; slot < next_fresh_slot_; ++slot)
            {
                Entry* const entry = find_entry(slot);
                if (entry && entry->live.load(std::memory_order_relaxed))
                    intervals += entry->coherence.retire_history();
            }
            return intervals;
        }

        HandleStatus ResidencyDirectory::record_host_write(GlobalHandle handle)
        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            HandleStatus status = HandleStatus::Ok;
            Entry* const entry = live_entry(handle, status);
            if (!entry)
                return status;
            const std::uint64_t generation = host_write_generation_.load(std::memory_order_relaxed) + 1;
            // The mark is stored first, so a reader that sees the new generation sees the mark.
            entry->host_write.store(generation, std::memory_order_release);
            host_write_generation_.store(generation, std::memory_order_release);
            return HandleStatus::Ok;
        }

        std::uint64_t ResidencyDirectory::host_write_of(GlobalHandle handle) const noexcept
        {
            HandleStatus status = HandleStatus::Ok;
            const Entry* const entry = live_entry(handle, status);
            if (!entry)
                return 0;
            const std::uint64_t mark = entry->host_write.load(std::memory_order_acquire);
            // A retire between the two loads may have cleared the mark of another handle.
            if (entry->generation.load(std::memory_order_acquire) != handle.generation())
                return 0;
            return mark;
        }

        ErrorCode error_code_of(HandleStatus status) noexcept
        {
            switch (status)
            {
                case HandleStatus::StaleHandle:     return ErrorCode::StaleHandle;
                case HandleStatus::NullHandle:      return ErrorCode::InvalidArgument;
                case HandleStatus::ForeignUniverse: return ErrorCode::InvalidArgument;
                case HandleStatus::NotResident:     return ErrorCode::InvalidAccess;
                case HandleStatus::SpaceExhausted:  return ErrorCode::AllocationFailed;
                case HandleStatus::Ok:              break;
            }
            return ErrorCode::InvalidState;
        }

        Result<void*> to_result(const ResolveOutcome& outcome)
        {
            if (outcome.status == HandleStatus::Ok)
                return outcome.address;
            const ErrorCode code = error_code_of(outcome.status);
            const char* message = "the handle did not resolve";
            switch (outcome.status)
            {
                case HandleStatus::StaleHandle:
                    message = "the handle's slot was retired or reused since it was minted";
                    break;
                case HandleStatus::NullHandle:
                    message = "the handle is the null handle";
                    break;
                case HandleStatus::ForeignUniverse:
                    message = "the handle was minted by another runtime instance";
                    break;
                case HandleStatus::NotResident:
                    message = "the handle has no instance on the asked memory space";
                    break;
                case HandleStatus::SpaceExhausted:
                case HandleStatus::Ok:
                    break;
            }
            return make_error(Detail::error_without_throwing(code, message));
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
