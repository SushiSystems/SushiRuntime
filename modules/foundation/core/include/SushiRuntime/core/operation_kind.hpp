/**************************************************************************/
/* operation_kind.hpp                                                     */
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
 * @file operation_kind.hpp
 * @brief Declares OperationKind, the consumer-named class of work a node's operations belong to.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/core/operation_id.hpp>

namespace SushiRuntime
{
    /**
     * @brief Identifies a class of work by the 32-bit FNV-1a hash of its name.
     *
     * The consumer names its kinds; the runtime holds no list of them and only keys a measured
     * throughput on the value. A default-constructed kind is OperationKinds::K_GENERIC.
     * @see modules/foundation/core/README.md
     */
    struct OperationKind
    {
        /** @brief The hash of the kind's name, or 0 for the generic kind. */
        std::uint32_t value = 0;

        /** @brief Builds the generic kind. */
        constexpr OperationKind() noexcept = default;

        /** @brief Builds a kind from a hash already computed. */
        constexpr explicit OperationKind(std::uint32_t hash_value) noexcept : value(hash_value)
        {
        }

        /**
         * @brief Returns the kind a name hashes to.
         * @param name   Bytes of the name.
         * @param length How many bytes of name are hashed.
         */
        [[nodiscard]] static constexpr OperationKind named(const char* name,
                                                           std::size_t length) noexcept
        {
            return OperationKind(OperationID::hash(name, length));
        }

        /** @brief Returns whether this is the generic kind. */
        [[nodiscard]] constexpr bool is_generic() const noexcept { return value == 0; }

        /** @brief Returns whether both kinds hold the same hash. */
        [[nodiscard]] constexpr bool operator==(const OperationKind& other) const noexcept
        {
            return value == other.value;
        }

        /** @brief Returns whether the kinds hold different hashes. */
        [[nodiscard]] constexpr bool operator!=(const OperationKind& other) const noexcept
        {
            return value != other.value;
        }

        /** @brief Orders two kinds by their hash. */
        [[nodiscard]] constexpr bool operator<(const OperationKind& other) const noexcept
        {
            return value < other.value;
        }
    };

    /**
     * @brief Holds the few kinds any consumer may share.
     * @see modules/foundation/core/README.md
     */
    namespace OperationKinds
    {
        /** @brief Work with no kind of its own; it takes a device's base throughput. */
        inline constexpr OperationKind K_GENERIC{};

        /** @brief Work bounded by memory bandwidth, the hash of "memory_bound". */
        inline constexpr OperationKind K_MEMORY_BOUND = OperationKind::named("memory_bound", 12);

        /** @brief Work bounded by arithmetic throughput, the hash of "compute_bound". */
        inline constexpr OperationKind K_COMPUTE_BOUND = OperationKind::named("compute_bound", 13);
    } // namespace OperationKinds

    inline namespace Literals
    {
        /** @brief Builds the OperationKind of a string literal, as in "gemm"_kind. */
        [[nodiscard]] constexpr OperationKind operator""_kind(const char* name,
                                                              std::size_t length) noexcept
        {
            return OperationKind::named(name, length);
        }
    } // namespace Literals
} // namespace SushiRuntime
