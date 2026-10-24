/**************************************************************************/
/* placement_rules.hpp                                                    */
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
 * @file placement_rules.hpp
 * @brief Declares PlacementRules, an ordered rule set that is checked on add and resolves nodes.
 * @author Mustafa Garip
 * @see modules/foundation/placement/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/placement_class.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>

namespace SushiRuntime
{
    /**
     * @brief Keeps the rules in the order they were added and picks the rule that wins for a node.
     * @see modules/foundation/placement/README.md
     */
    class SUSHIRUNTIME_API PlacementRules
    {
        public:
            /**
             * @brief Holds the longest Wait bound and deadline add() accepts: one day.
             * @see modules/foundation/placement/README.md
             */
            static constexpr std::chrono::milliseconds K_MAX_DURATION{24LL * 60 * 60 * 1000};

            /**
             * @brief Checks a rule and, when it passes, keeps it after the rules already held.
             * @return Nothing, or ErrorCode::InvalidArgument naming the rule and what to pass
             *         instead; a refused rule leaves the set unchanged.
             */
            [[nodiscard]] Result<void> add(PlacementRule rule);

            /**
             * @brief Returns the position of the winning rule: the largest priority, then the
             *        smallest position.
             * @param placement_class The node's class; an unnamed one matches only rules that
             *                        state no class.
             * @return The position, or none when no rule matches.
             */
            [[nodiscard]] std::optional<std::size_t>
            resolve(const PlacementClass& placement_class, std::optional<std::uint64_t> key,
                    OperationID operation) const noexcept;

            /** @brief Returns how many rules the set holds. */
            [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

            /** @brief Returns whether the set holds no rule. */
            [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

            /**
             * @brief Returns the rule at a position.
             * @pre position is less than size().
             */
            [[nodiscard]] const PlacementRule& rule(std::size_t position) const;

        private:
            /**
             * @brief Pairs a rule with what resolve compares, computed once on add.
             */
            struct Entry
            {
                /** @brief Says how the rule's class pattern is compared. */
                enum class ClassMatch : std::uint8_t
                {
                    Any,    /**< The rule states no class. */
                    Exact,  /**< The class hash equals class_hash. */
                    Prefix  /**< class_hash is among the node class's prefix hashes. */
                };

                PlacementRule rule;                       /**< The rule as it was added. */
                ClassMatch class_match = ClassMatch::Any; /**< How class_hash is compared. */
                std::uint32_t class_hash = 0;             /**< Hash of the class or prefix. */
                OperationID operation;                    /**< Unnamed when the rule has none. */
            };

            /** @brief The entries in insertion order. */
            std::vector<Entry> entries_;
    };
} // namespace SushiRuntime
