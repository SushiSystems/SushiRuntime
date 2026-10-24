/**************************************************************************/
/* test_placement_rules.cpp                                               */
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
 * @file test_placement_rules.cpp
 * @brief Tests PlacementRules: the match, the winner, and every refusal of add.
 * @author Mustafa Garip
 * @see tests/foundation/placement/README.md
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/placement_class.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Literals;

namespace
{
    /** @brief Builds a rule that matches a class pattern and targets the machine "worker". */
    PlacementRule class_rule(const std::string& name, const std::string& pattern, int priority)
    {
        PlacementRule rule;
        rule.name = name;
        rule.match.placement_class = pattern;
        rule.target.machine_name = "worker";
        rule.priority = priority;
        return rule;
    }

    /** @brief Builds a rule that passes every check. */
    PlacementRule valid_rule()
    {
        return class_rule("valid", "a.b", 0);
    }

    /** @brief Holds a node as the match sees it, and the position expected to win. */
    struct ResolveCase
    {
        const char* label;                 /**< Names the case in a failure. */
        PlacementClass placement_class;    /**< The node's class. */
        std::optional<std::uint64_t> key;  /**< The node's key. */
        OperationID operation;             /**< The node's operation. */
        std::optional<std::size_t> expect; /**< Position of the winning rule, or none. */
    };

    /** @brief Holds a rule that add must refuse and a fragment its message must hold. */
    struct RefusalCase
    {
        const char* label;    /**< Names the case in a failure. */
        PlacementRule rule;   /**< The rule add refuses. */
        const char* fragment; /**< Text the refusal message holds. */
    };

    /** @brief Returns a valid rule changed by a function. */
    template <class Change>
    PlacementRule changed(Change change)
    {
        PlacementRule rule = valid_rule();
        change(rule);
        return rule;
    }
} // namespace

/** @brief Tests that a set holds nothing before a rule is added. */
TEST(Small_PlacementRules, AnEmptySetHasNoRuleAndResolvesNothing)
{
    const PlacementRules rules;
    EXPECT_TRUE(rules.empty());
    EXPECT_EQ(rules.size(), 0u);
    EXPECT_FALSE(rules.resolve("a.b"_class, std::nullopt, "gemm"_op).has_value());
}

/** @brief Tests that rule() returns the rules in the order they were added. */
TEST(Small_PlacementRules, RuleReturnsTheRulesInInsertionOrder)
{
    PlacementRules rules;
    ASSERT_TRUE(rules.add(class_rule("first", "a.b", 0)).has_value());
    ASSERT_TRUE(rules.add(class_rule("second", "c.d", 9)).has_value());
    ASSERT_TRUE(rules.add(class_rule("third", "e.*", 1)).has_value());
    EXPECT_FALSE(rules.empty());
    ASSERT_EQ(rules.size(), 3u);
    EXPECT_EQ(rules.rule(0).name, "first");
    EXPECT_EQ(rules.rule(1).name, "second");
    EXPECT_EQ(rules.rule(2).name, "third");
    EXPECT_EQ(rules.rule(1).priority, 9);
}

/** @brief Tests that an exact class of any depth is accepted. */
TEST(Small_PlacementRules, AnExactClassOfAnyDepthIsAccepted)
{
    PlacementRules rules;
    ASSERT_TRUE(rules.add(class_rule("deep", "a.b.c.d.e.f.g", 0)).has_value());
    EXPECT_EQ(rules.resolve("a.b.c.d.e.f.g"_class, std::nullopt, "gemm"_op),
              std::optional<std::size_t>(0));
}

/** @brief Tests the match and the winner over a table of nodes. */
TEST(Small_PlacementRules, ResolveTableMatchesClassPrefixKeyOperationAndPriority)
{
    PlacementRules rules;
    PlacementRule exact = class_rule("exact", "sushiai.replica", 0);
    PlacementRule prefix = class_rule("prefix", "sushiengine.*", 0);
    PlacementRule keyed = class_rule("keyed", "sushiai.replica", 5);
    keyed.match.key = 3;
    PlacementRule operation = class_rule("operation", "", 1);
    operation.match.operation = "gemm";
    PlacementRule any_class_key = class_rule("any-class-key", "", 2);
    any_class_key.match.key = 9;
    PlacementRule low = class_rule("low", "conflict.a", 4);
    PlacementRule high = class_rule("high", "conflict.a", 7);
    PlacementRule tie_first = class_rule("tie-first", "tie.x", 3);
    PlacementRule tie_second = class_rule("tie-second", "tie.x", 3);
    PlacementRule zero_key = class_rule("zero-key", "zero.key", 0);
    zero_key.match.key = 0;
    for (PlacementRule* rule : {&exact, &prefix, &keyed, &operation, &any_class_key, &low, &high,
                                &tie_first, &tie_second, &zero_key})
        ASSERT_TRUE(rules.add(*rule).has_value()) << rule->name;

    const std::vector<ResolveCase> cases = {
        {"exact class", "sushiai.replica"_class, std::nullopt, "vec.add"_op, 0},
        {"exact class, key the keyed rule wants", "sushiai.replica"_class, 3, "vec.add"_op, 2},
        {"exact class, another key", "sushiai.replica"_class, 4, "vec.add"_op, 0},
        {"prefix, class below it", "sushiengine.physics"_class, std::nullopt, "vec.add"_op, 1},
        {"prefix, class two below it", "sushiengine.matter.water"_class, std::nullopt,
         "vec.add"_op, 1},
        {"prefix never matches its own class", "sushiengine"_class, std::nullopt, "vec.add"_op,
         std::nullopt},
        {"operation rule matches any class", "other.thing"_class, std::nullopt, "gemm"_op, 3},
        {"operation rule matches a node with no class", PlacementClass(), std::nullopt, "gemm"_op,
         3},
        {"operation rule outranks the exact rule", "sushiai.replica"_class, std::nullopt,
         "gemm"_op, 3},
        {"keyed rule outranks the operation rule", "sushiai.replica"_class, 3, "gemm"_op, 2},
        {"key rule matches any class", "other.thing"_class, 9, "vec.add"_op, 4},
        {"larger priority beats the operation rule", PlacementClass(), 9, "gemm"_op, 4},
        {"larger priority wins a conflict", "conflict.a"_class, std::nullopt, "vec.add"_op, 6},
        {"equal priority goes to the first added", "tie.x"_class, std::nullopt, "vec.add"_op, 7},
        {"key 0 is a key like any other", "zero.key"_class, 0, "vec.add"_op, 9},
        {"a key rule needs a key", "zero.key"_class, std::nullopt, "vec.add"_op, std::nullopt},
        {"no rule matches", "other.thing"_class, std::nullopt, "vec.add"_op, std::nullopt},
        {"no class and no rule for it", PlacementClass(), std::nullopt, "vec.add"_op,
         std::nullopt},
    };
    for (const ResolveCase& item : cases)
    {
        SCOPED_TRACE(item.label);
        EXPECT_EQ(rules.resolve(item.placement_class, item.key, item.operation), item.expect);
    }
}

/** @brief Tests that add refuses every refusal of the design table and keeps nothing. */
TEST(Small_PlacementRules, AddRefusesEveryCaseOfTheRefusedWhenColumn)
{
    const std::vector<RefusalCase> cases = {
        {"empty match", changed([](PlacementRule& r) { r.match.placement_class.clear(); }),
         "no class, key or operation"},
        {"empty segment in the middle",
         changed([](PlacementRule& r) { r.match.placement_class = "a..b"; }), "empty segment"},
        {"empty first segment",
         changed([](PlacementRule& r) { r.match.placement_class = ".a"; }), "empty segment"},
        {"empty last segment",
         changed([](PlacementRule& r) { r.match.placement_class = "a."; }), "empty segment"},
        {"prefix without its dot star",
         changed([](PlacementRule& r) { r.match.placement_class = "a*"; }), "without its"},
        {"a lone star", changed([](PlacementRule& r) { r.match.placement_class = "*"; }),
         "without its"},
        {"a lone dot star",
         changed([](PlacementRule& r) { r.match.placement_class = ".*"; }), "empty segment"},
        {"star in the middle",
         changed([](PlacementRule& r) { r.match.placement_class = "a.*.b"; }),
         "outside a trailing"},
        {"prefix deeper than the class keeps",
         changed([](PlacementRule& r) { r.match.placement_class = "a.b.c.d.e.*"; }),
         "at most 4"},
        {"no machine and no device",
         changed([](PlacementRule& r) { r.target = PlacementTarget(); }),
         "no machine and no device"},
        {"two machine fields",
         changed([](PlacementRule& r) { r.target.machine_label = NodeLabel{"zone", "north"}; }),
         "machine twice"},
        {"two device fields",
         changed([](PlacementRule& r)
                 {
                     r.target.device_index = 0;
                     r.target.device_kind = DeviceKind::GPU;
                 }),
         "device twice"},
        {"three device fields",
         changed([](PlacementRule& r)
                 {
                     r.target.device_index = 0;
                     r.target.device_kind = DeviceKind::GPU;
                     r.target.device_min_memory = 1024;
                 }),
         "device twice"},
        {"label with an empty key",
         changed([](PlacementRule& r)
                 {
                     r.target.machine_name.clear();
                     r.target.machine_label = NodeLabel{"", "north"};
                 }),
         "empty key"},
        {"label key holding equals",
         changed([](PlacementRule& r)
                 {
                     r.target.machine_name.clear();
                     r.target.machine_label = NodeLabel{"zone=a", "north"};
                 }),
         "holds '='"},
        {"wait with a zero bound",
         changed([](PlacementRule& r)
                 {
                     r.if_absent.action = AbsentAction::Wait;
                     r.if_absent.bound = std::chrono::milliseconds(0);
                 }),
         "bound that is not positive"},
        {"wait with a negative bound",
         changed([](PlacementRule& r)
                 {
                     r.if_absent.action = AbsentAction::Wait;
                     r.if_absent.bound = std::chrono::milliseconds(-5);
                 }),
         "bound that is not positive"},
        {"zero deadline",
         changed([](PlacementRule& r) { r.deadline = std::chrono::milliseconds(0); }),
         "deadline is not positive"},
        {"negative deadline",
         changed([](PlacementRule& r) { r.deadline = std::chrono::milliseconds(-1); }),
         "deadline is not positive"},
    };

    PlacementRules rules;
    for (const RefusalCase& item : cases)
    {
        SCOPED_TRACE(item.label);
        Result<void> added = rules.add(item.rule);
        ASSERT_FALSE(added.has_value());
        EXPECT_EQ(added.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(added.error().message.find("rule \"valid\""), std::string::npos)
            << added.error().message;
        EXPECT_NE(added.error().message.find(item.fragment), std::string::npos)
            << added.error().message;
    }
    EXPECT_TRUE(rules.empty());
}

/** @brief Tests that a refused rule without a name is named by its position. */
TEST(Small_PlacementRules, ARefusalNamesAnUnnamedRuleByItsPosition)
{
    PlacementRules rules;
    ASSERT_TRUE(rules.add(valid_rule()).has_value());
    PlacementRule unnamed = valid_rule();
    unnamed.name.clear();
    unnamed.deadline = std::chrono::milliseconds(0);
    Result<void> added = rules.add(unnamed);
    ASSERT_FALSE(added.has_value());
    EXPECT_NE(added.error().message.find("position 1"), std::string::npos)
        << added.error().message;
    EXPECT_EQ(rules.size(), 1u);
}

/** @brief Tests that a wait with a positive bound and a positive deadline are accepted. */
TEST(Small_PlacementRules, APositiveWaitBoundAndDeadlineAreAccepted)
{
    PlacementRules rules;
    PlacementRule rule = valid_rule();
    rule.if_absent.action = AbsentAction::Wait;
    rule.if_absent.bound = std::chrono::milliseconds(250);
    rule.deadline = std::chrono::milliseconds(1000);
    EXPECT_TRUE(rules.add(rule).has_value());
    EXPECT_EQ(rules.size(), 1u);
}

/** @brief Tests that a Wait bound or a deadline above K_MAX_DURATION is refused, and it is not. */
TEST(Small_PlacementRules, Regression_AWaitBoundOrDeadlineAboveTheMaximumIsRefused)
{
    const std::chrono::milliseconds above =
        PlacementRules::K_MAX_DURATION + std::chrono::milliseconds(1);
    PlacementRules rules;

    PlacementRule waits = valid_rule();
    waits.if_absent.action = AbsentAction::Wait;
    waits.if_absent.bound = std::chrono::milliseconds::max();
    Result<void> refused = rules.add(waits);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(refused.error().message.find("longest bound"), std::string::npos)
        << refused.error().message;
    waits.if_absent.bound = above;
    EXPECT_FALSE(rules.add(waits).has_value());

    PlacementRule late = valid_rule();
    late.deadline = above;
    refused = rules.add(late);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(refused.error().message.find("deadline"), std::string::npos)
        << refused.error().message;
    EXPECT_EQ(rules.size(), 0u);

    waits.if_absent.bound = PlacementRules::K_MAX_DURATION;
    late.deadline = PlacementRules::K_MAX_DURATION;
    EXPECT_TRUE(rules.add(waits).has_value());
    EXPECT_TRUE(rules.add(late).has_value());
}
