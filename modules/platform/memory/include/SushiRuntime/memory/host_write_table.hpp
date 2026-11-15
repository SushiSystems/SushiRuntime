/**************************************************************************/
/* host_write_table.hpp                                                   */
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
 * @file host_write_table.hpp
 * @brief Declares HostWriteTable, the HostWriteLog that keeps the host-write marks of the handles.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/memory/host_write_log.hpp>
#include <SushiRuntime/memory/residency_reader.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Keeps, per handle slot, the generation of the last host write on the handle.
         *
         * The table asks the residency view only whether a handle is live; it holds no residency
         * state. A mark carries the generation of the handle it was made for, so a slot that is
         * retired and reused does not show the old mark. Writers serialise on one mutex; the
         * reads take no lock.
         */
        class HostWriteTable final : public HostWriteLog
        {
            public:
                /** @brief Number of marks in one chunk. */
                static constexpr std::uint32_t K_CHUNK_MARKS = 1024;

                /** @brief Number of chunks the table can grow to. */
                static constexpr std::uint32_t K_MAX_CHUNKS = 4096;

                /**
                 * @brief Builds an empty table that checks liveness through @p residency.
                 * @param residency Must outlive the table.
                 */
                explicit HostWriteTable(const ResidencyReader& residency);

                /** @brief Frees every chunk. */
                ~HostWriteTable();

                HostWriteTable(const HostWriteTable&) = delete;
                HostWriteTable& operator=(const HostWriteTable&) = delete;
                HostWriteTable(HostWriteTable&&) = delete;
                HostWriteTable& operator=(HostWriteTable&&) = delete;

                /**
                 * @brief Records that the host wrote bytes of handle, under the next generation.
                 * @return Ok, the status of a handle that is not live, or SpaceExhausted when the
                 *         slot is past the table.
                 */
                [[nodiscard]] HandleStatus record_host_write(GlobalHandle handle) override;

                /** @brief Returns the generation of the last host write on any handle, or 0; any thread. */
                [[nodiscard]] std::uint64_t host_write_generation() const noexcept override;

                /** @brief Returns the generation of the last host write on handle, or 0 for none or a stale handle. */
                [[nodiscard]] std::uint64_t host_write_of(GlobalHandle handle) const noexcept override;

            private:
                /** @brief Holds the mark of one slot. */
                struct Mark
                {
                    /// @brief Generation of the handle the mark was made for.
                    std::atomic<std::uint32_t> handle_generation{0};

                    /// @brief Host-write generation of the last write, or 0.
                    std::atomic<std::uint64_t> written{0};
                };

                /** @brief Holds K_CHUNK_MARKS marks; a chunk never moves once made. */
                struct Chunk
                {
                    std::array<Mark, K_CHUNK_MARKS> marks; /**< The marks. */
                };

                /** @brief Returns the mark of slot, or null when its chunk is absent. */
                [[nodiscard]] Mark* find_mark(std::uint32_t slot) const noexcept;

                /// @brief Tells whether a handle is live.
                const ResidencyReader& residency_;

                /// @brief Chunk pointers; slot s lives in chunk s / K_CHUNK_MARKS.
                std::unique_ptr<std::atomic<Chunk*>[]> chunks_;

                /// @brief Serialises the writers.
                std::mutex writer_mutex_;

                /// @brief Generation of the last host write on any handle.
                std::atomic<std::uint64_t> generation_{0};
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
