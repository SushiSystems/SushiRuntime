/**************************************************************************/
/* nd_kernel_math.hpp                                                     */
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
 * @file nd_kernel_math.hpp
 * @brief Declares the index math and arithmetic of the tests' NdRange kernels as host functions of plain ids.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

namespace TestSupport
{
    /**
     * @brief Squares one element in place.
     * @param values The elements.
     * @param index The element to square.
     */
    inline void square_in_place(int* values, std::size_t index) noexcept
    {
        values[index] = values[index] * values[index];
    }

    /**
     * @brief Writes the row-major linear index of a cell into the cell.
     * @param cells The grid, row-major with @p width cells per row.
     * @param x The column.
     * @param y The row.
     * @param width The cells per row.
     */
    inline void write_linear_index(int* cells, std::size_t x, std::size_t y, std::size_t width) noexcept
    {
        cells[y * width + x] = static_cast<int>(y * width + x);
    }

    /**
     * @brief Sets one element to zero.
     * @param words The elements.
     * @param index The element to clear.
     */
    template <typename Word>
    inline void clear_word(Word* words, std::size_t index) noexcept
    {
        words[index] = Word{0};
    }
} // namespace TestSupport
