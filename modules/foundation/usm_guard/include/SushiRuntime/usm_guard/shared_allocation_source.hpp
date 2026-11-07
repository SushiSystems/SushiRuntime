/**************************************************************************/
/* shared_allocation_source.hpp                                           */
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
 * @file shared_allocation_source.hpp
 * @brief Declares SharedAllocationSource, the port USMShared allocates through.
 * @author Mustafa Garip
 * @see modules/foundation/usm_guard/README.md
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Hands out memory the host and a device both reach, and takes it back.
         * @see modules/foundation/usm_guard/README.md
         */
        class SharedAllocationSource
        {
            public:
                virtual ~SharedAllocationSource() = default;

                /**
                 * @brief Returns @p bytes aligned to @p alignment, or null when none is granted.
                 * @param alignment A power of two.
                 */
                [[nodiscard]] virtual void* allocate(std::size_t bytes, std::size_t alignment) = 0;

                /**
                 * @brief Frees what allocate() returned.
                 * @param address Not null; returned by allocate() of this source.
                 */
                virtual void free(void* address) noexcept = 0;

            protected:
                SharedAllocationSource() = default;
                SharedAllocationSource(const SharedAllocationSource&) = default;
                SharedAllocationSource& operator=(const SharedAllocationSource&) = default;
        };
    } // namespace Test
} // namespace SushiRuntime
