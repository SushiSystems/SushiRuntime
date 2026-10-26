/**************************************************************************/
/* drop_counts.hpp                                                        */
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
 * @file drop_counts.hpp
 * @brief Declares DropCause and DropCounts, which say how many dispatches a run left out of its timing.
 * @author Mustafa Garip
 * @see modules/foundation/report/README.md
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    /** @brief Names why a dispatch left no record in a run. */
    enum class DropCause : std::uint8_t
    {
        UnreadableEvent, /**< The event's timestamps could not be read. */
        FullRow,         /**< The node's row had no free slot. */
        UnknownNode,     /**< The registration index is past the ledger's rows. */
        NoEvent,         /**< A kernel dispatch returned no event to read. */
        NoQueue          /**< The device queue was not found, so the dispatch took an unprofiled one. */
    };

    /** @brief Holds how many dispatches a run lost per cause and the first node that lost one. */
    struct DropCounts
    {
        /** @brief Number of causes in DropCause. */
        static constexpr std::size_t K_CAUSES = 5;

        /** @brief Marks a count that names no node. */
        static constexpr std::uint32_t K_NO_NODE = 0xFFFFFFFFu;

        std::array<std::uint32_t, K_CAUSES> per_cause{}; /**< Dispatches lost, indexed by DropCause. */
        std::uint32_t first_node = K_NO_NODE;            /**< Registration index of the first dispatch lost. */

        /** @brief Returns whether any dispatch was lost. */
        [[nodiscard]] bool any() const noexcept
        {
            return std::any_of(per_cause.begin(), per_cause.end(),
                               [](std::uint32_t count) { return count != 0; });
        }

        /** @brief Returns how many dispatches were lost for @p cause. */
        [[nodiscard]] std::uint32_t of(DropCause cause) const noexcept
        {
            return per_cause[static_cast<std::size_t>(cause)];
        }

        /** @brief Returns how many dispatches were lost for every cause together. */
        [[nodiscard]] std::uint64_t total() const noexcept
        {
            std::uint64_t sum = 0;
            for (const std::uint32_t count : per_cause)
                sum += count;
            return sum;
        }
    };
} // namespace SushiRuntime
