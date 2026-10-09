/**************************************************************************/
/* strong_id.hpp                                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file strong_id.hpp
 * @brief Declares StrongId, a strong typedef for integer identifiers.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
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
         * @see modules/foundation/core/README.md
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
