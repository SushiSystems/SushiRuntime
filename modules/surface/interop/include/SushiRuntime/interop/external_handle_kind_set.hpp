/**************************************************************************/
/* external_handle_kind_set.hpp                                           */
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
 * @file external_handle_kind_set.hpp
 * @brief Declares a set of external handle kinds held as one bit per kind.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/interop/external_memory.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds a set of external handle kinds, one bit per enumerator.
     */
    class ExternalHandleKindSet
    {
        public:
            /// @brief Adds @p kind to the set.
            constexpr void insert(ExternalHandleKind kind) noexcept
            {
                bits_ = static_cast<std::uint8_t>(bits_ | bit_of(kind));
            }

            /// @brief Removes @p kind from the set.
            constexpr void erase(ExternalHandleKind kind) noexcept
            {
                bits_ = static_cast<std::uint8_t>(bits_ & ~bit_of(kind));
            }

            /// @brief Tells whether @p kind is in the set.
            [[nodiscard]] constexpr bool contains(ExternalHandleKind kind) const noexcept
            {
                return (bits_ & bit_of(kind)) != 0;
            }

            /// @brief Tells whether the set holds no kind.
            [[nodiscard]] constexpr bool is_empty() const noexcept
            {
                return bits_ == 0;
            }

            /// @brief Compares two sets by their members.
            [[nodiscard]] constexpr bool operator==(const ExternalHandleKindSet& other) const noexcept
            {
                return bits_ == other.bits_;
            }

            /// @brief Compares two sets by their members.
            [[nodiscard]] constexpr bool operator!=(const ExternalHandleKindSet& other) const noexcept
            {
                return bits_ != other.bits_;
            }

        private:
            /// @brief Returns the bit that stands for @p kind.
            static constexpr std::uint8_t bit_of(ExternalHandleKind kind) noexcept
            {
                return static_cast<std::uint8_t>(1u << static_cast<unsigned>(kind));
            }

            /// @brief Holds one bit per enumerator of ExternalHandleKind.
            std::uint8_t bits_ = 0;
    };
} // namespace SushiRuntime
