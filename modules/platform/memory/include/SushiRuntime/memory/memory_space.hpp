/**************************************************************************/
/* memory_space.hpp                                                       */
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
 * @file memory_space.hpp
 * @brief Declares MemorySpace, one place an instance of a buffer can live.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief One place an instance can live: a device, pinned host memory of a NUMA node, or a
         *        file.
         *
         * Each kind is its own class behind this interface, so a new kind adds a file.
         */
        class MemorySpace
        {
            public:
                virtual ~MemorySpace() = default;

                /** @brief Returns the id the residency directory names this space by. */
                [[nodiscard]] virtual MemorySpaceID id() const noexcept = 0;

                /** @brief Returns the name of the kind of this space, for logs only; code asks the queries above. */
                [[nodiscard]] virtual std::string_view kind_name() const noexcept = 0;

                /**
                 * @brief Returns whether this space is host memory, whose bytes a copy reads or writes
                 *        with one memcpy and no staging.
                 */
                [[nodiscard]] virtual bool is_host_memory() const noexcept = 0;

                /** @brief Returns whether the bytes of this space are a file range, read through a stream. */
                [[nodiscard]] virtual bool is_file_backed() const noexcept = 0;

                /** @brief Returns the bytes of the pool this space draws on. */
                [[nodiscard]] virtual std::size_t capacity_bytes() const noexcept = 0;

                /** @brief Returns the bytes of the pool others held when the space was built. */
                [[nodiscard]] virtual std::size_t reserve_bytes() const noexcept = 0;

                /**
                 * @brief Returns the id of the pool this space draws on.
                 *
                 * Spaces that share one pool, such as an integrated GPU and the host, return the
                 * same id, and the pool is counted once.
                 */
                [[nodiscard]] virtual std::uint32_t shared_pool() const noexcept = 0;

                /** @brief Returns the spaces this space copies to without staging, sorted. */
                [[nodiscard]] virtual const std::vector<MemorySpaceID>& direct_targets() const noexcept = 0;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
