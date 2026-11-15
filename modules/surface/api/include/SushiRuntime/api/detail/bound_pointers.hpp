/**************************************************************************/
/* bound_pointers.hpp                                                     */
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
 * @file bound_pointers.hpp
 * @brief Declares bound_pointers(), the pointers a capture launch receives at dispatch.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/bound_operands.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns the operands the node being dispatched on this thread bound.
         * @param count Number of operands the caller reads.
         * @return An empty view when no node bound a table, or when its size is not @p count.
         */
        [[nodiscard]] SUSHIRUNTIME_API BoundOperands dispatch_pointer_table(std::size_t count) noexcept;

        /**
         * @brief Builds the tuple of @p operands' pointers, each cast to the type of its slot.
         *
         * A capture launch holds the pointer types it recorded, so the cast is its own and the
         * view is read untyped.
         */
        template <class Pointers, std::size_t... Is>
        Pointers pointers_from_table(const BoundOperands& operands, std::index_sequence<Is...>) noexcept
        {
            return Pointers(static_cast<std::tuple_element_t<Is, Pointers>>(operands.untyped(Is))...);
        }

        /**
         * @brief Returns the pointers a capture launch hands its kernel on this dispatch.
         * @param recorded The pointers recorded at add(), the home instances.
         * @return The pointers the dispatching node bound on its device, or @p recorded when it
         *         bound none.
         */
        template <class Pointers>
        Pointers bound_pointers(const Pointers& recorded) noexcept
        {
            constexpr std::size_t K_COUNT = std::tuple_size_v<Pointers>;
            const BoundOperands operands = dispatch_pointer_table(K_COUNT);
            if (operands.is_empty())
                return recorded;
            return pointers_from_table<Pointers>(operands, std::make_index_sequence<K_COUNT>{});
        }
    } // namespace Detail
} // namespace SushiRuntime
