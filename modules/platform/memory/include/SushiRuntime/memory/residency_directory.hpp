/**************************************************************************/
/* residency_directory.hpp                                                */
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
 * @file residency_directory.hpp
 * @brief Declares ResidencyDirectory, the slot table that mints, retires and resolves handles.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>
#include <SushiRuntime/handle/handle_space.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/coherence_intervals.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Holds the size and alignment of one element of a buffer. */
        struct ElementLayout
        {
            std::size_t size = 1;      /**< Bytes per element. */
            std::size_t alignment = 1; /**< Alignment of an element in bytes. */
        };

        /** @brief Holds what a reader sees of one live entry. */
        struct EntryView
        {
            std::size_t size = 0;                   /**< Bytes of the buffer. */
            ElementLayout layout;                   /**< Layout of one element. */
            MemorySpaceID home;                     /**< Space the buffer was made on. */
            std::uint64_t valid_spaces = 0;         /**< Bit i set when space i holds an instance. */
            HandleStatus status = HandleStatus::Ok; /**< Ok, or why nothing was read. */
        };

        /**
         * @brief Maps handles to the instances of their buffer on each memory space.
         *
         * Writers (mint, retire, describe, set_instance, drop_instance) serialise on one mutex.
         * Readers (resolve, is_stale, inspect) take no lock and never wait on a writer.
         */
        class ResidencyDirectory final : public HandleSpace, public HandleResolver
        {
            public:
                /** @brief Number of memory spaces an entry can hold an instance on. */
                static constexpr std::uint32_t K_MAX_MEMORY_SPACES = 64;

                /** @brief Number of entries in one chunk. */
                static constexpr std::uint32_t K_CHUNK_ENTRIES = 1024;

                /** @brief Number of chunks the table can grow to. */
                static constexpr std::uint32_t K_MAX_CHUNKS = 4096;

                /** @brief Builds an empty directory that mints handles of the given universe. */
                explicit ResidencyDirectory(std::uint32_t universe);

                /** @brief Frees every chunk. */
                ~ResidencyDirectory() override;

                ResidencyDirectory(const ResidencyDirectory&) = delete;
                ResidencyDirectory& operator=(const ResidencyDirectory&) = delete;
                ResidencyDirectory(ResidencyDirectory&&) = delete;
                ResidencyDirectory& operator=(ResidencyDirectory&&) = delete;

                /** @brief Mints a handle on the lowest free slot, with no instance on any space. */
                [[nodiscard]] MintOutcome mint(HandleKind kind, HandleFlags flags) override;

                /** @brief Retires handle; its slot returns to the free list under a new generation. */
                [[nodiscard]] HandleStatus retire(GlobalHandle handle) override;

                /** @brief Resolves handle to its instance on space without taking a lock. */
                [[nodiscard]] ResolveOutcome resolve(GlobalHandle handle,
                                                     MemorySpaceID space) const override;

                /** @brief Returns whether handle's slot was retired or reused since it was minted. */
                [[nodiscard]] bool is_stale(GlobalHandle handle) const override;

                /** @brief Records the size, element layout and home space of a live handle. */
                [[nodiscard]] HandleStatus describe(GlobalHandle handle, std::size_t size,
                                                    ElementLayout layout, MemorySpaceID home);

                /**
                 * @brief Records address as handle's instance on space.
                 * @return Ok, NullHandle, StaleHandle, ForeignUniverse, or NotResident when space
                 *         is past K_MAX_MEMORY_SPACES or address is null.
                 */
                [[nodiscard]] HandleStatus set_instance(GlobalHandle handle, MemorySpaceID space,
                                                        void* address);

                /** @brief Forgets handle's instance on space; the memory itself is not freed. */
                [[nodiscard]] HandleStatus drop_instance(GlobalHandle handle, MemorySpaceID space);

                /** @brief Returns the size, layout, home and valid spaces of a live handle. */
                [[nodiscard]] EntryView inspect(GlobalHandle handle) const;

                /** @brief Returns the universe this directory mints handles in. */
                [[nodiscard]] std::uint32_t universe() const noexcept
                {
                    return universe_;
                }

                /**
                 * @brief Returns the coherence map of a live handle, or null when it is not live.
                 *
                 * The planner is the map's only writer, at plan time; the map takes no lock.
                 */
                [[nodiscard]] CoherenceIntervals* coherence(GlobalHandle handle) noexcept;

                /** @brief Returns the coherence map of a live handle, or null when it is not live. */
                [[nodiscard]] const CoherenceIntervals* coherence(GlobalHandle handle) const noexcept;

                /** @brief Returns the address of the entry of slot, or null when its chunk is absent. */
                [[nodiscard]] const void* entry_address(std::uint32_t slot) const noexcept;

            private:
                /** @brief Holds one slot of the table. */
                struct Entry
                {
                    /// @brief Generation of the live handle, or of the next one while the slot is free.
                    std::atomic<std::uint32_t> generation{1};

                    /// @brief Whether a handle of the current generation is live.
                    std::atomic<bool> live{false};

                    /// @brief Bytes of the buffer.
                    std::atomic<std::size_t> size{0};

                    /// @brief Bytes per element.
                    std::atomic<std::size_t> element_size{1};

                    /// @brief Alignment of an element.
                    std::atomic<std::size_t> element_alignment{1};

                    /// @brief Space the buffer was made on.
                    std::atomic<std::uint32_t> home{0};

                    /// @brief Bit i set when instances[i] holds an address.
                    std::atomic<std::uint64_t> valid_spaces{0};

                    /// @brief Address of the instance on each space, or null.
                    std::array<std::atomic<void*>, K_MAX_MEMORY_SPACES> instances{};

                    /// @brief Valid spaces and version of each byte range; written by the planner only.
                    CoherenceIntervals coherence;
                };

                /** @brief Holds K_CHUNK_ENTRIES entries; a chunk never moves once made. */
                struct Chunk
                {
                    std::array<Entry, K_CHUNK_ENTRIES> entries; /**< The entries. */
                };

                /** @brief Returns the entry of slot, or null when its chunk is absent. */
                [[nodiscard]] Entry* find_entry(std::uint32_t slot) const noexcept;

                /** @brief Returns the entry handle names when handle is live, with the status. */
                [[nodiscard]] Entry* live_entry(GlobalHandle handle, HandleStatus& status) const noexcept;

                /** @brief Returns the lowest free slot, making its chunk when needed; false when full. */
                [[nodiscard]] bool take_slot(std::uint32_t& slot);

                /// @brief Universe every handle of this directory carries.
                std::uint32_t universe_;

                /// @brief Chunk pointers; slot s lives in chunk s / K_CHUNK_ENTRIES.
                std::unique_ptr<std::atomic<Chunk*>[]> chunks_;

                /// @brief Serialises the writers.
                std::mutex writer_mutex_;

                /// @brief Retired slots, lowest first.
                std::priority_queue<std::uint32_t, std::vector<std::uint32_t>,
                                    std::greater<std::uint32_t>> free_slots_;

                /// @brief Lowest slot never handed out.
                std::uint32_t next_fresh_slot_ = 0;
        };

        /**
         * @brief Returns the ErrorCode a failed handle operation is reported with.
         * @pre status is not HandleStatus::Ok.
         */
        [[nodiscard]] ErrorCode error_code_of(HandleStatus status) noexcept;

        /** @brief Returns the resolved address, or the Error the outcome's status maps to. */
        [[nodiscard]] Result<void*> to_result(const ResolveOutcome& outcome);
    } // namespace Internal::Memory
} // namespace SushiRuntime
