/****************************************************************/
/* strong_id.hpp                                                */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file strong_id.hpp
 * @brief Declares StrongId, a strong typedef for integer identifiers.
 * @author Mustafa Garip
 * @see include/SushiRuntime/core/README.md
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Wraps an integer in a distinct type identified by a phantom Tag.
         * @tparam Tag A phantom type that names the identifier's domain.
         * @tparam Rep The underlying integer representation.
         * @see include/SushiRuntime/core/README.md
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
