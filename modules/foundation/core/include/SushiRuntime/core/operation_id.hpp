/**************************************************************************/
/* operation_id.hpp                                                       */
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
 * @file operation_id.hpp
 * @brief Declares OperationID, the hashed name of an operation, and its _op literal.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Identifies an operation by the 32-bit FNV-1a hash of its name.
     * @see modules/foundation/core/README.md
     */
    struct OperationID
    {
        /** @brief Stands for "no operation": the value of a default-constructed id. */
        static constexpr std::uint32_t K_NONE = 0;

        /** @brief The hash of the operation's name, or K_NONE. */
        std::uint32_t value = K_NONE;

        /** @brief Builds the id that names no operation. */
        constexpr OperationID() noexcept = default;

        /** @brief Builds an id from a hash already computed. */
        constexpr explicit OperationID(std::uint32_t hash_value) noexcept : value(hash_value)
        {
        }

        /**
         * @brief Returns the 32-bit FNV-1a hash of a name.
         * @param name   Bytes of the name; each is read as unsigned.
         * @param length How many bytes of name are hashed.
         */
        [[nodiscard]] static constexpr std::uint32_t hash(const char* name,
                                                          std::size_t length) noexcept
        {
            std::uint32_t result = 2166136261u;
            for (std::size_t i = 0; i < length; ++i)
            {
                result ^= static_cast<std::uint32_t>(static_cast<unsigned char>(name[i]));
                result *= 16777619u;
            }
            return result;
        }

        /** @brief Returns whether this id names an operation. */
        [[nodiscard]] constexpr bool is_named() const noexcept { return value != K_NONE; }

        /** @brief Returns whether both ids hold the same hash. */
        [[nodiscard]] constexpr bool operator==(const OperationID& other) const noexcept
        {
            return value == other.value;
        }

        /** @brief Returns whether the ids hold different hashes. */
        [[nodiscard]] constexpr bool operator!=(const OperationID& other) const noexcept
        {
            return value != other.value;
        }

        /** @brief Orders two ids by their hash. */
        [[nodiscard]] constexpr bool operator<(const OperationID& other) const noexcept
        {
            return value < other.value;
        }
    };

    /**
     * @brief Holds the literals of the library.
     * @see modules/foundation/core/README.md
     */
    inline namespace Literals
    {
        /** @brief Builds the OperationID of a string literal, as in "vec.add"_op. */
        [[nodiscard]] constexpr OperationID operator""_op(const char* name,
                                                          std::size_t length) noexcept
        {
            return OperationID(OperationID::hash(name, length));
        }
    } // namespace Literals
} // namespace SushiRuntime
