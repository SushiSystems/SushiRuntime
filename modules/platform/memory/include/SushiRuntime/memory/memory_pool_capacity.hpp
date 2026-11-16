/**************************************************************************/
/* memory_pool_capacity.hpp                                               */
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
 * @file memory_pool_capacity.hpp
 * @brief Declares usable_pool_bytes, the bytes a set of spaces offers with each pool counted once.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/memory_space.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Returns the sum over pools of capacity less reserve, each pool counted once.
         *
         * Of the spaces that share a pool, the one with the lowest id speaks for it.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::size_t usable_pool_bytes(const std::vector<const MemorySpace*>& spaces);
    } // namespace Internal::Memory
} // namespace SushiRuntime
