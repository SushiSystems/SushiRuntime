/**************************************************************************/
/* memory_probe.hpp                                                       */
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
 * @file memory_probe.hpp
 * @brief Declares MemoryProbe, the port a memory space reads its total and free bytes through.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Holds the total and free bytes of one memory pool at the time it was probed. */
        struct ProbedMemory
        {
            std::size_t total_bytes = 0; /**< Bytes the pool holds. */
            std::size_t free_bytes = 0;  /**< Bytes nobody held when probed. */
        };

        /** @brief Reads the total and free bytes of one memory pool. */
        class MemoryProbe
        {
            public:
                virtual ~MemoryProbe() = default;

                /** @brief Returns the total and free bytes of the pool now. */
                [[nodiscard]] virtual ProbedMemory probe() const = 0;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
