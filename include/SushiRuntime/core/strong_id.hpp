/****************************************************************/
/* strong_id.hpp                                                */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
