/**************************************************************************/
/* buffer_contents.hpp                                                    */
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
 * @file buffer_contents.hpp
 * @brief Reads and writes the whole of a host-accessible Buffer or State, so a test body stays a straight line.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace TestSupport
{

/**
 * @brief Copies the host-visible elements of @p handle into a vector.
 * @tparam Handle A Buffer or State whose memory the host can address.
 * @param handle The handle to read.
 * @return One element per element of @p handle, in order.
 */
template <class Handle>
[[nodiscard]] auto contents(const Handle& handle)
{
    const auto view = handle.host();
    return std::vector<std::remove_cv_t<std::remove_reference_t<decltype(view[0])>>>(view.begin(), view.end());
}

/**
 * @brief Sets every host-visible element of @p handle to @p value.
 * @tparam Handle A Buffer or State whose memory the host can address.
 * @param handle The handle to write.
 * @param value The value each element takes.
 */
template <class Handle, class Value>
void fill_with(const Handle& handle, const Value& value)
{
    const auto view = handle.host();
    std::fill(view.begin(), view.end(), value);
}

/**
 * @brief Copies @p values into the first elements of @p handle.
 * @tparam Handle A Buffer or State whose memory the host can address.
 * @param handle The handle to write.
 * @param values At most handle.size() values.
 */
template <class Handle, class Value>
void assign_from(const Handle& handle, const std::vector<Value>& values)
{
    const auto view = handle.host();
    std::copy(values.begin(), values.end(), view.begin());
}

/**
 * @brief Returns @p count elements that start at @p first and rise by one.
 * @tparam Value The element type.
 * @param count How many elements.
 * @param first The first element.
 */
template <class Value>
[[nodiscard]] std::vector<Value> ramp(std::size_t count, Value first = Value{})
{
    std::vector<Value> values(count);
    for (std::size_t i = 0; i < count; ++i)
        values[i] = static_cast<Value>(first + static_cast<Value>(i));
    return values;
}

/**
 * @brief Returns @p capacity elements equal to @p inside on [@p base, @p base + @p count) and to @p outside elsewhere.
 * @tparam Value The element type.
 * @param capacity The length of the result.
 * @param base First element of the window.
 * @param count Length of the window; the window is cut at @p capacity.
 */
template <class Value>
[[nodiscard]] std::vector<Value> window_of(std::size_t capacity, std::size_t base, std::size_t count,
                                           Value inside, Value outside)
{
    std::vector<Value> values(capacity, outside);
    for (std::size_t i = base; i < base + count && i < capacity; ++i)
        values[i] = inside;
    return values;
}

} // namespace TestSupport
