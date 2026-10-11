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
 * @brief Declares StrongID, a strong typedef for integer identifiers.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstddef>
#include <functional>

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
        class StrongID
        {
            public:
                /** @brief Constructs the zero identifier. */
                constexpr StrongID() noexcept : raw_(Rep{}) {}

                /** @brief Wraps @p v as an identifier of this domain. */
                explicit constexpr StrongID(Rep v) noexcept : raw_(v) {}

                /**
                 * @brief Returns the wrapped integer value.
                 * @return The integer this identifier wraps.
                 */
                [[nodiscard]] constexpr Rep value() const noexcept { return raw_; }

                /** @brief True when both identifiers wrap the same integer. */
                [[nodiscard]] friend constexpr bool operator==(StrongID a, StrongID b) noexcept
                {
                    return a.raw_ == b.raw_;
                }

                /** @brief True when the identifiers wrap different integers. */
                [[nodiscard]] friend constexpr bool operator!=(StrongID a, StrongID b) noexcept
                {
                    return a.raw_ != b.raw_;
                }

                /** @brief True when the integer of @p a is less than that of @p b. */
                [[nodiscard]] friend constexpr bool operator<(StrongID a, StrongID b) noexcept
                {
                    return a.raw_ < b.raw_;
                }

            private:
                Rep raw_; /**< The wrapped integer value. */
        };

    } // namespace Core
} // namespace SushiRuntime

namespace std
{
    /**
     * @brief Hashes a StrongID by the integer it wraps.
     * @tparam Tag The phantom domain tag of the identifier.
     * @tparam Rep The underlying integer representation.
     */
    template <class Tag, class Rep>
    struct hash<SushiRuntime::Core::StrongID<Tag, Rep>>
    {
        /** @brief Returns the hash of the integer that @p id wraps. */
        [[nodiscard]] std::size_t operator()(SushiRuntime::Core::StrongID<Tag, Rep> id) const
        {
            return std::hash<Rep>{}(id.value());
        }
    };
} // namespace std
