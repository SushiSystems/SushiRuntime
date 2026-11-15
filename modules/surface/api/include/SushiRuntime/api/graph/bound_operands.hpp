/**************************************************************************/
/* bound_operands.hpp                                                      */
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
 * @file bound_operands.hpp
 * @brief Declares BoundOperands and bound_operands(), the pointers a task body bound by handle reads.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    /**
     * @brief Views the operand pointers of the task being dispatched, bound on its device.
     *
     * Valid only during the body; it holds one pointer per handle, in the order the task named them.
     */
    class BoundOperands final
    {
        public:
            /** @brief Views no pointer. */
            BoundOperands() noexcept = default;

            /** @brief Views @p count pointers starting at @p table. */
            BoundOperands(void* const* table, std::size_t count) noexcept
                : table_(table), count_(count)
            {

            }

            /** @brief Returns the pointer of operand @p index; @p index must be below size(). */
            [[nodiscard]] void* operator[](std::size_t index) const noexcept
            {
                return table_[index];
            }

            /** @brief Returns the pointer of operand @p index as a T*. */
            template <class T>
            [[nodiscard]] T* as(std::size_t index) const noexcept
            {
                return static_cast<T*>(table_[index]);
            }

            /** @brief Returns the first pointer of the table. */
            [[nodiscard]] void* const* data() const noexcept { return table_; }

            /** @brief Returns the number of operands. */
            [[nodiscard]] std::size_t size() const noexcept { return count_; }

            /** @brief Tells whether the view holds no pointer. */
            [[nodiscard]] bool is_empty() const noexcept { return count_ == 0; }

            /** @brief Returns the first pointer, for a range for. */
            [[nodiscard]] void* const* begin() const noexcept { return table_; }

            /** @brief Returns one past the last pointer, for a range for. */
            [[nodiscard]] void* const* end() const noexcept { return table_ + count_; }

        private:
            /// @brief The bound table of the dispatching node.
            void* const* table_ = nullptr;

            /// @brief Number of pointers in table_.
            std::size_t count_ = 0;
    };

    /**
     * @brief Returns the operand pointers the task being dispatched on this thread bound on its
     *        device, the ones its body hands every kernel it submits.
     * @return An empty view outside such a body.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API BoundOperands bound_operands() noexcept;
} // namespace SushiRuntime