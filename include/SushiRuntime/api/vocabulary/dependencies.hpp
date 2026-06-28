/**************************************************************************/
/* dependencies.hpp                                                       */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/core/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            // Pulls the read/write dependency region out of either data handle. A
            // Buffer maps to its whole-buffer region; a State exposes its current
            // buffer as the read region and its next buffer as the write region.
            // These free functions let the Reads/Writes wrappers accept handles
            // uniformly, and an explicit ResourceRegion (e.g. buf.region(off, len))
            // passes through, so a caller can name a sub-range to read or write.
            template <typename T>
            inline Core::ResourceRegion read_key_of(const Buffer<T>& b) noexcept { return b.dependency_key(); }
            template <typename T>
            inline Core::ResourceRegion read_key_of(const State<T>& s) noexcept { return s.read_key(); }
            inline Core::ResourceRegion read_key_of(const Core::ResourceRegion& r) noexcept { return r; }
            template <typename T>
            inline Core::ResourceRegion write_key_of(const Buffer<T>& b) noexcept { return b.dependency_key(); }
            template <typename T>
            inline Core::ResourceRegion write_key_of(const State<T>& s) noexcept { return s.write_key(); }
            inline Core::ResourceRegion write_key_of(const Core::ResourceRegion& r) noexcept { return r; }
        } // namespace detail

        /**
         * @brief Names the data a task reads, as a list of dependency keys.
         *
         * The ergonomic counterpart to a hand-built std::vector<void*>: pass the
         * Buffers and States a task reads and this collects their read keys, so the
         * caller never spells dependency_key()/read_key() by hand. Used by the
         * explicit-tracking and offload add() overloads to order tasks by data.
         */
        struct Reads
        {
            std::vector<Core::ResourceRegion> keys;  /**< Read regions, in argument order. */

            /** @brief Collects the read region of each handle or sub-region passed. */
            template <class... Handles>
            explicit Reads(const Handles&... hs) : keys{ detail::read_key_of(hs)... } {}
        };

        /**
         * @brief Names the data a task writes, as a list of dependency keys.
         *
         * As Reads, but gathers write keys (a State contributes its next buffer).
         */
        struct Writes
        {
            std::vector<Core::ResourceRegion> keys;  /**< Write regions, in argument order. */

            /** @brief Collects the write region of each handle or sub-region passed. */
            template <class... Handles>
            explicit Writes(const Handles&... hs) : keys{ detail::write_key_of(hs)... } {}
        };

    } // namespace API
} // namespace SushiRuntime
