/**************************************************************************/
/* placement_class.hpp                                                    */
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
 * @file placement_class.hpp
 * @brief Declares PlacementClass, the hashed dotted name of a node's group, and its _class literal.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <SushiRuntime/core/operation_id.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the group a node belongs to by the hash of a dotted name, and keeps the hash
     *        of each prefix of that name.
     * @see modules/foundation/core/README.md
     */
    struct PlacementClass
    {
        /** @brief How many prefix hashes a class keeps; deeper prefixes are not kept. */
        static constexpr std::size_t K_MAX_PREFIX_DEPTH = 4;

        /** @brief Stands for "no class": the value of a default-constructed class. */
        static constexpr std::uint32_t K_NONE = 0;

        /** @brief The 32-bit FNV-1a hash of the whole name, or K_NONE. */
        std::uint32_t value = K_NONE;

        /** @brief The first prefix_count entries are the hashes of the name's prefixes. */
        std::array<std::uint32_t, K_MAX_PREFIX_DEPTH> prefixes{};

        /** @brief How many entries of prefixes are set. */
        std::size_t prefix_count = 0;

        /** @brief Builds the class that names no group. */
        constexpr PlacementClass() noexcept = default;

        /**
         * @brief Builds the class of a dotted name.
         * @param name   Bytes of the name.
         * @param length How many bytes of name are read.
         *
         * Each non-empty prefix that ends right before a '.' is hashed, in order, up to
         * K_MAX_PREFIX_DEPTH of them.
         */
        constexpr PlacementClass(const char* name, std::size_t length) noexcept
            : value(OperationID::hash(name, length))
        {
            for (std::size_t i = 1; i < length && prefix_count < K_MAX_PREFIX_DEPTH; ++i)
            {
                if (name[i] == '.')
                    prefixes[prefix_count++] = OperationID::hash(name, i);
            }
        }

        /** @brief Returns whether this class names a group. */
        [[nodiscard]] constexpr bool is_named() const noexcept { return value != K_NONE; }

        /**
         * @brief Returns whether a hash is one of the kept prefixes.
         *
         * A prefix never matches the whole name, so "a.*" matches "a.b" and not "a".
         */
        [[nodiscard]] constexpr bool has_prefix(std::uint32_t prefix_hash) const noexcept
        {
            for (std::size_t i = 0; i < prefix_count; ++i)
            {
                if (prefixes[i] == prefix_hash)
                    return true;
            }
            return false;
        }

        /** @brief Returns whether both classes hold the same whole-name hash. */
        [[nodiscard]] constexpr bool operator==(const PlacementClass& other) const noexcept
        {
            return value == other.value;
        }

        /** @brief Returns whether the classes hold different whole-name hashes. */
        [[nodiscard]] constexpr bool operator!=(const PlacementClass& other) const noexcept
        {
            return value != other.value;
        }
    };

    /**
     * @brief Holds the literals of the library.
     * @see modules/foundation/core/README.md
     */
    inline namespace Literals
    {
        /** @brief Builds the PlacementClass of a string literal, as in "sushiai.replica"_class. */
        [[nodiscard]] constexpr PlacementClass operator""_class(const char* name,
                                                                std::size_t length) noexcept
        {
            return PlacementClass(name, length);
        }
    } // namespace Literals
} // namespace SushiRuntime
