/**************************************************************************/
/* in_memory_shared_allocation_source.hpp                                 */
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
 * @file in_memory_shared_allocation_source.hpp
 * @brief Declares InMemorySharedAllocationSource, a SharedAllocationSource of host memory.
 * @author Mustafa Garip
 * @see modules/foundation/usm_guard/README.md
 */

#pragma once

#include <cstddef>
#include <new>

#include <SushiRuntime/usm_guard/shared_allocation_source.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Allocates host memory on a 64-byte boundary and counts what it handed out
         *        and took back.
         *
         * An alignment above 64 bytes is refused with null.
         * @see modules/foundation/usm_guard/README.md
         */
        class InMemorySharedAllocationSource final : public SharedAllocationSource
        {
            public:
                [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment) override
                {
                    if (alignment > K_ALIGNMENT)
                        return nullptr;
                    ++allocations_;
                    return ::operator new(bytes, std::align_val_t{K_ALIGNMENT});
                }

                void free(void* address) noexcept override
                {
                    ++frees_;
                    ::operator delete(address, std::align_val_t{K_ALIGNMENT});
                }

                /** @brief Returns how many allocations the source made. */
                [[nodiscard]] std::size_t allocations() const noexcept { return allocations_; }

                /** @brief Returns how many allocations the source freed. */
                [[nodiscard]] std::size_t frees() const noexcept { return frees_; }

            private:
                /// @brief The alignment of every allocation.
                static constexpr std::size_t K_ALIGNMENT = 64;
                /// @brief How many allocations the source made.
                std::size_t allocations_ = 0;
                /// @brief How many allocations the source freed.
                std::size_t frees_ = 0;
        };
    } // namespace Test
} // namespace SushiRuntime
