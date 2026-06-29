/**************************************************************************/
/* strong_id.hpp                                                          */
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

/**
 * @file strong_id.hpp
 * @brief A zero-overhead strong typedef for integer identifiers.
 *
 * A bare std::size_t parameter lets unrelated quantities be passed in each other's
 * place — a device index where an element count belongs — and the compiler stays
 * silent. StrongId wraps an integer in a distinct type keyed by a tag, so the
 * compiler rejects the mix while the value still occupies exactly one integer and
 * the explicit constructor keeps construction intentional.
 */

#include <cstddef>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief A distinct integer type identified by a phantom @p Tag.
         *
         * Two StrongId instantiations with different tags are unrelated types even
         * when their underlying representation matches, so one cannot be passed where
         * the other is expected. The constructor is explicit, so a raw integer never
         * converts implicitly; the value is read through value(). Trivial and the size
         * of @p Rep, so passing one costs nothing over passing the integer.
         *
         * @tparam Tag A phantom type that names the identifier's domain.
         * @tparam Rep The underlying integer representation.
         */
        template <class Tag, class Rep = std::size_t>
        struct StrongId
        {
            Rep raw;  /**< The wrapped integer value. */

            /** @brief Constructs the zero identifier. */
            constexpr StrongId() noexcept : raw(Rep{}) {}

            /** @brief Wraps @p v as an identifier of this domain. */
            explicit constexpr StrongId(Rep v) noexcept : raw(v) {}

            /** @brief Returns the wrapped integer value. */
            constexpr Rep value() const noexcept { return raw; }

            friend constexpr bool operator==(StrongId a, StrongId b) noexcept
            {
                return a.raw == b.raw;
            }
            friend constexpr bool operator!=(StrongId a, StrongId b) noexcept
            {
                return a.raw != b.raw;
            }
        };

    } // namespace Core
} // namespace SushiRuntime
