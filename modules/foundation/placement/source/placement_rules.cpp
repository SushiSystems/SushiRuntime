/**************************************************************************/
/* placement_rules.cpp                                                    */
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
 * @file placement_rules.cpp
 * @brief Implements PlacementRules: the checks of add and the match of resolve.
 * @author Mustafa Garip
 * @see modules/foundation/placement/README.md
 */

#include <SushiRuntime/placement/placement_rules.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/placement_class.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Holds how a rule's class pattern compares, or why the pattern is refused. */
        struct ClassPattern
        {
            bool is_any = true;     /**< The pattern is empty. */
            bool is_prefix = false; /**< The pattern ended in ".*". */
            std::uint32_t hash = 0; /**< Hash of the whole class or of the prefix. */
            std::string refusal;    /**< Not empty when the pattern breaks a rule. */
        };

        /** @brief Returns whether a dotted name is empty or has an empty segment. */
        [[nodiscard]] bool has_empty_segment(const std::string& dotted)
        {
            return dotted.empty() || dotted.front() == '.' || dotted.back() == '.' ||
                   dotted.find("..") != std::string::npos;
        }

        /** @brief Returns how many dot-separated segments a non-empty dotted name holds. */
        [[nodiscard]] std::size_t segment_count(const std::string& dotted)
        {
            return static_cast<std::size_t>(std::count(dotted.begin(), dotted.end(), '.')) + 1;
        }

        /** @brief Returns the 32-bit hash of a dotted name, the one PlacementClass keeps. */
        [[nodiscard]] std::uint32_t hash_of(const std::string& dotted) noexcept
        {
            return OperationID::hash(dotted.data(), dotted.size());
        }

        /** @brief Reads a class pattern into how it compares, or into the reason it is refused. */
        [[nodiscard]] ClassPattern parse_class_pattern(const std::string& pattern)
        {
            ClassPattern parsed;
            if (pattern.empty())
                return parsed;
            parsed.is_any = false;
            const std::string quoted = "the class \"" + pattern + "\" ";

            const std::size_t star = pattern.find('*');
            const bool trailing_prefix_mark = star != std::string::npos &&
                                              star + 1 == pattern.size() && star >= 1 &&
                                              pattern[star - 1] == '.';
            if (star != std::string::npos && !trailing_prefix_mark)
            {
                if (star + 1 == pattern.size())
                    parsed.refusal = quoted + "is a prefix without its \".*\"; write \"" +
                                     pattern.substr(0, star) +
                                     ".*\" to match every class below it, or drop the star to "
                                     "match one class.";
                else
                    parsed.refusal = quoted + "holds a star outside a trailing \".*\"; pass "
                                              "\"a.b\" for one class or \"a.b.*\" for every "
                                              "class below \"a.b\".";
                return parsed;
            }

            const std::string body = trailing_prefix_mark ? pattern.substr(0, star - 1) : pattern;
            if (has_empty_segment(body))
            {
                parsed.refusal = quoted + "has an empty segment; pass dot-separated names with "
                                          "no empty part, such as \"a.b\".";
                return parsed;
            }
            if (trailing_prefix_mark && segment_count(body) > PlacementClass::K_MAX_PREFIX_DEPTH)
            {
                parsed.refusal = quoted + "is a prefix of " + std::to_string(segment_count(body)) +
                                 " segments, and a class keeps at most " +
                                 std::to_string(PlacementClass::K_MAX_PREFIX_DEPTH) +
                                 "; pass a prefix of at most that many segments, or the exact "
                                 "class.";
                return parsed;
            }
            parsed.is_prefix = trailing_prefix_mark;
            parsed.hash = hash_of(body);
            return parsed;
        }

        /** @brief Returns the text that names a rule in an error: its name, else its position. */
        [[nodiscard]] std::string where_of(const PlacementRule& rule, std::size_t position)
        {
            if (!rule.name.empty())
                return "PlacementRules::add, rule \"" + rule.name + "\": ";
            return "PlacementRules::add, rule at position " + std::to_string(position) + ": ";
        }

        /** @brief Returns the reason the match of a rule is refused, or an empty string. */
        [[nodiscard]] std::string match_refusal(const PlacementMatch& match)
        {
            if (match.placement_class.empty() && !match.key.has_value() && match.operation.empty())
                return "the match states no class, key or operation; set at least one of "
                       "match.placement_class, match.key and match.operation.";
            return std::string();
        }

        /** @brief Returns the reason the target of a rule is refused, or an empty string. */
        [[nodiscard]] std::string target_refusal(const PlacementTarget& target)
        {
            const int machine_fields = (target.machine_name.empty() ? 0 : 1) +
                                       (target.machine_label.has_value() ? 1 : 0);
            const int device_fields = (target.device_index.has_value() ? 1 : 0) +
                                      (target.device_kind.has_value() ? 1 : 0) +
                                      (target.device_min_memory.has_value() ? 1 : 0);
            if (machine_fields == 0 && device_fields == 0)
                return "the target names no machine and no device; set target.machine_name, "
                       "target.machine_label or one device field.";
            if (machine_fields > 1)
                return "the target names a machine twice; pass target.machine_name or "
                       "target.machine_label, not both.";
            if (device_fields > 1)
                return "the target names a device twice; pass one of target.device_index, "
                       "target.device_kind and target.device_min_memory.";
            if (target.machine_label.has_value())
            {
                const std::string& key = target.machine_label->key;
                if (key.empty())
                    return "target.machine_label has an empty key; pass a key of at least one "
                           "character.";
                if (key.find('=') != std::string::npos)
                    return "target.machine_label key \"" + key +
                           "\" holds '='; pass the key without it, and put what follows the '=' "
                           "in the value.";
            }
            return std::string();
        }

        /** @brief Returns the reason the policy of a rule is refused, or an empty string. */
        [[nodiscard]] std::string policy_refusal(const PlacementRule& rule)
        {
            if (rule.if_absent.action == AbsentAction::Wait &&
                rule.if_absent.bound <= std::chrono::milliseconds::zero())
                return "if_absent waits with a bound that is not positive; pass a bound above "
                       "0 ms, or use Local or Fail.";
            const std::string most = std::to_string(PlacementRules::K_MAX_DURATION.count());
            if (rule.if_absent.action == AbsentAction::Wait &&
                rule.if_absent.bound > PlacementRules::K_MAX_DURATION)
                return "if_absent waits " + std::to_string(rule.if_absent.bound.count()) +
                       " ms, above the longest bound of " + most +
                       " ms; pass a bound of at most that, or use Fail.";
            if (rule.deadline.has_value() && *rule.deadline <= std::chrono::milliseconds::zero())
                return "the deadline is not positive; pass a deadline above 0 ms, or leave it "
                       "unset.";
            if (rule.deadline.has_value() && *rule.deadline > PlacementRules::K_MAX_DURATION)
                return "the deadline of " + std::to_string(rule.deadline->count()) +
                       " ms is above the longest of " + most +
                       " ms; pass a deadline of at most that, or leave it unset.";
            return std::string();
        }
    } // namespace

    Result<void> PlacementRules::add(PlacementRule rule)
    {
        std::optional<Error> refusal;
        Result<void> translated = Detail::translate_exceptions(
            ErrorCode::AllocationFailed,
            [this, &rule, &refusal]()
            {
                ClassPattern pattern = parse_class_pattern(rule.match.placement_class);
                std::string reason = match_refusal(rule.match);
                if (reason.empty())
                    reason = std::move(pattern.refusal);
                if (reason.empty())
                    reason = target_refusal(rule.target);
                if (reason.empty())
                    reason = policy_refusal(rule);
                if (!reason.empty())
                {
                    refusal = Error{ErrorCode::InvalidArgument,
                                    where_of(rule, entries_.size()) + reason};
                    return;
                }

                Entry entry;
                if (!pattern.is_any)
                    entry.class_match = pattern.is_prefix ? Entry::ClassMatch::Prefix
                                                          : Entry::ClassMatch::Exact;
                entry.class_hash = pattern.hash;
                if (!rule.match.operation.empty())
                    entry.operation = OperationID(hash_of(rule.match.operation));
                entry.rule = std::move(rule);
                entries_.push_back(std::move(entry));
            });
        if (!translated)
            return make_error(std::move(translated.error()));
        if (refusal)
            return make_error(std::move(*refusal));
        return Result<void>();
    }

    std::optional<std::size_t> PlacementRules::resolve(const PlacementClass& placement_class,
                                                       std::optional<std::uint64_t> key,
                                                       OperationID operation) const noexcept
    {
        std::optional<std::size_t> winner;
        for (std::size_t position = 0; position < entries_.size(); ++position)
        {
            const Entry& entry = entries_[position];
            if (winner && entry.rule.priority <= entries_[*winner].rule.priority)
                continue;
            if (entry.class_match == Entry::ClassMatch::Exact &&
                placement_class.value != entry.class_hash)
                continue;
            if (entry.class_match == Entry::ClassMatch::Prefix &&
                !placement_class.has_prefix(entry.class_hash))
                continue;
            if (entry.rule.match.key.has_value() && entry.rule.match.key != key)
                continue;
            if (entry.operation.is_named() && entry.operation != operation)
                continue;
            winner = position;
        }
        return winner;
    }

    const PlacementRule& PlacementRules::rule(std::size_t position) const
    {
        SR_VERIFY(position < entries_.size(), "PlacementRules::rule needs a position below size()");
        return entries_[position].rule;
    }
} // namespace SushiRuntime
