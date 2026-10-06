/**************************************************************************/
/* test_rule_registration.cpp                                             */
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
 * @file test_rule_registration.cpp
 * @brief Tests Graph::rule_registration, which maps an add() call to its registration index.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t WIDTH = 64;
    constexpr std::size_t PLAIN_RULES = 3;

    /** @brief Holds one graph, the rules it recorded and where the primitive's rules start. */
    struct Recorded
    {
        API::Graph graph;                 /**< The graph under test. */
        API::Buffer<float> values;        /**< The plain kernels' target. */
        API::Buffer<std::uint32_t> counts;/**< The scan's input. */
        API::Buffer<std::uint32_t> offsets;/**< The scan's output. */
        API::Buffer<std::uint32_t> total; /**< The scan's total. */
        std::size_t primitive_first = 0;  /**< First rule the scan recorded. */
    };

    /**
     * @brief Records three plain kernels and one prefix scan onto a fresh graph.
     *
     * The scan is the interesting half: one public call lowers to several nodes
     * that share one registration index, which is what makes the add() order and
     * the report's numbering diverge.
     */
    Recorded record(Runtime& rt)
    {
        Recorded r{rt.graph(), rt.buffer<float>(WIDTH), rt.buffer<std::uint32_t>(WIDTH),
                   rt.buffer<std::uint32_t>(WIDTH + 1), rt.buffer<std::uint32_t>(1), 0};
        for (std::size_t i = 0; i < WIDTH; ++i)
        {
            r.values[i] = 1.0f;
            r.counts[i] = static_cast<std::uint32_t>(i % 7);
        }
        for (std::size_t i = 0; i <= WIDTH; ++i)
            r.offsets[i] = 0;

        float* v = r.values.data();
        for (std::size_t k = 0; k < PLAIN_RULES; ++k)
            r.graph.add(Reads(r.values), Writes(r.values), WIDTH,
                        [v](std::size_t i) { v[i] = v[i] * 1.000001f; });

        r.primitive_first = r.graph.size();
        API::LateCount live;
        live.capacity = WIDTH;
        live.host = [] { return WIDTH; };
        r.graph.add_prefix_scan(r.counts, r.offsets, r.total, live);
        r.graph.set_profiling_requested(true);
        return r;
    }
} // namespace

TEST(Integration_RuleRegistration, ARuleCarriesNoIndexUntilTheGraphCompiles)
{
    auto rt = Runtime::create();
    Recorded r = record(rt);

    ASSERT_GT(r.graph.size(), PLAIN_RULES) << "the scan must have recorded at least one rule";
    for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
        EXPECT_EQ(r.graph.rule_registration(rule), API::Graph::no_registration)
            << "rule " << rule << " reported an index before the first run";
}

TEST(Integration_RuleRegistration, OnePublicCallsRulesShareOneIndex)
{
    auto rt = Runtime::create();
    Recorded r = record(rt);
    Core::RunReport report;
    r.graph.run(1, report);
    ASSERT_TRUE(report.is_successful);

    std::set<std::uint32_t> plain;
    for (std::size_t rule = 0; rule < PLAIN_RULES; ++rule)
    {
        const std::uint32_t index = r.graph.rule_registration(rule);
        EXPECT_NE(index, API::Graph::no_registration);
        plain.insert(index);
    }
    EXPECT_EQ(plain.size(), PLAIN_RULES) << "a plain add() must take an index of its own";

    const std::uint32_t shared = r.graph.rule_registration(r.primitive_first);
    EXPECT_NE(shared, API::Graph::no_registration);
    for (std::size_t rule = r.primitive_first; rule < r.graph.size(); ++rule)
        EXPECT_EQ(r.graph.rule_registration(rule), shared)
            << "rule " << rule << " of one add_prefix_scan took a different index";
    EXPECT_EQ(plain.count(shared), 0u) << "the scan must not reuse a plain rule's index";
}

TEST(Integration_RuleRegistration, EveryNodeEventNamesARecordedRule)
{
    auto rt = Runtime::create();
    Recorded r = record(rt);
    Core::RunReport report;
    r.graph.run(1, report);
    ASSERT_TRUE(report.is_successful);
    ASSERT_FALSE(report.node_events.empty()) << "profiling was requested, so events must exist";

    std::set<std::uint32_t> recorded;
    for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
        recorded.insert(r.graph.rule_registration(rule));

    for (const Core::NodeEventRecord& event : report.node_events)
        EXPECT_EQ(recorded.count(event.node_index), 1u)
            << "node event " << event.node_index << " matches no rule the caller recorded";
}
