/**************************************************************************/
/* file_memory_space.hpp                                                  */
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
 * @file file_memory_space.hpp
 * @brief Declares FileMemorySpace, the read-only memory space of one byte range of a file.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/memory_space.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Names a byte range of a file. */
        struct FileRange
        {
            std::string path;        /**< Path of the file. */
            std::uint64_t offset = 0; /**< First byte of the range in the file. */
            std::size_t bytes = 0;   /**< Bytes in the range. */
        };

        /**
         * @brief The read-only memory space of one byte range of a file.
         *
         * Its capacity is the range size, it has no reserve, it draws on a pool of its own and
         * no device addresses it. A buffer's instance on it is the token instance_address()
         * returns; file_offset() turns an address inside that instance into a file offset.
         */
        class SUSHIRUNTIME_API FileMemorySpace final : public MemorySpace
        {
            public:
                /**
                 * @param id Id of the space.
                 * @param range The bytes of the file the space holds.
                 */
                FileMemorySpace(MemorySpaceID id, FileRange range);

                [[nodiscard]] MemorySpaceID id() const noexcept override
                {
                    return id_;
                }

                [[nodiscard]] bool is_host_memory() const noexcept override
                {
                    return false;
                }

                [[nodiscard]] bool is_file_backed() const noexcept override
                {
                    return true;
                }

                [[nodiscard]] std::string_view kind_name() const noexcept override
                {
                    return "file";
                }

                [[nodiscard]] std::size_t capacity_bytes() const noexcept override
                {
                    return range_.bytes;
                }

                [[nodiscard]] std::size_t reserve_bytes() const noexcept override
                {
                    return 0;
                }

                /** @brief Returns the id of the space, the pool only this space draws on. */
                [[nodiscard]] std::uint32_t shared_pool() const noexcept override
                {
                    return id_.value();
                }

                /** @brief Returns no space: every copy out of a file goes through a transfer path. */
                [[nodiscard]] const std::vector<MemorySpaceID>& direct_targets() const noexcept override
                {
                    return direct_targets_;
                }

                /** @brief Returns the byte range of the file the space holds. */
                [[nodiscard]] const FileRange& range() const noexcept
                {
                    return range_;
                }

                /**
                 * @brief Returns the address the residency directory records as a buffer's
                 *        instance on this space.
                 *
                 * It is a fixed token, never dereferenced, so it is the same in every run.
                 */
                [[nodiscard]] static void* instance_address() noexcept;

                /**
                 * @brief Returns the file offset of address, an address inside the instance.
                 * @pre address is instance_address() plus at most range().bytes.
                 */
                [[nodiscard]] std::uint64_t file_offset(const void* address) const noexcept;

            private:
                /// @brief Id of the space.
                MemorySpaceID id_;

                /// @brief The bytes of the file the space holds.
                FileRange range_;

                /// @brief Empty: no space is reached without a transfer path.
                std::vector<MemorySpaceID> direct_targets_;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
