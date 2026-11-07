/**************************************************************************/
/* kernel_math.hpp                                                     */
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
 * @file kernel_math.hpp
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
     * @brief Writes an element's own index into it.
     * @param values The elements.
     * @param index The element to write.
     */
    inline void write_index(int* values, std::size_t index) noexcept
    {
        values[index] = static_cast<int>(index);
    }

    /**
     * @brief Writes twice an element's index into it.
     * @param values The elements.
     * @param index The element to write.
     */
    inline void write_double_index(int* values, std::size_t index) noexcept
    {
        values[index] = static_cast<int>(index) * 2;
    }

    /**
     * @brief Stores a value in one element.
     * @param words The elements.
     * @param index The element to write.
     * @param value The value to store.
     */
    template <typename Word>
    inline void store_value(Word* words, std::size_t index, Word value) noexcept
    {
        words[index] = value;
    }

    /**
     * @brief Adds a value to one element.
     * @param words The elements.
     * @param index The element to change.
     * @param value The value to add.
     */
    template <typename Word>
    inline void add_value(Word* words, std::size_t index, Word value) noexcept
    {
        words[index] += value;
    }

    /**
     * @brief Stores the width of a range in the first element.
     * @param words The elements.
     * @param begin The first index of the range.
     * @param end The index after the last one.
     */
    inline void store_range_width(int* words, std::size_t begin, std::size_t end) noexcept
    {
        words[0] = static_cast<int>(end - begin);
    }

    /**
     * @brief Stores twice a source element in a target element.
     * @param source The elements read.
     * @param target The elements written.
     * @param index The element to map.
     */
    template <typename Word>
    inline void store_doubled(const Word* source, Word* target, std::size_t index) noexcept
    {
        target[index] = source[index] * Word{2};
    }

    /**
     * @brief Stores a source element plus one in a target element.
     * @param source The elements read.
     * @param target The elements written.
     * @param index The element to map.
     */
    template <typename Word>
    inline void store_successor(const Word* source, Word* target, std::size_t index) noexcept
    {
        target[index] = source[index] + Word{1};
    }

    /**
     * @brief Stores an element's ordinal, its index plus one, in it.
     * @param words The elements.
     * @param index The element to write.
     */
    template <typename Word>
    inline void store_ordinal(Word* words, std::size_t index) noexcept
    {
        words[index] = static_cast<Word>(index) + Word{1};
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
