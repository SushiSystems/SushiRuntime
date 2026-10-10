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
 * @brief Tests Graph::rule_registration, which maps a rule to its registration index.
 * @see modules/surface/api/tests/README.md
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
    constexpr std::size_t RULE_NODES = 3;

    /** @brief Holds one graph, the buffers it names and the position of the grouped rule. */
    struct Recorded
    {
        API::Graph graph;                  /**< The graph under test. */
        API::Buffer<float> values;         /**< The plain kernels' target. */
        API::Buffer<std::uint32_t> first;  /**< The rule's input and final output. */
        API::Buffer<std::uint32_t> second; /**< What the rule's first node writes. */
        API::Buffer<std::uint32_t> third;  /**< What the rule's second node writes. */
        std::size_t grouped = 0;           /**< Position of the three-node rule. */
    };

    /**
     * @brief Records RULE_NODES kernels in one rule scope, each reading what the last wrote.
     * @param first,second,third Hold WIDTH words each; must outlive the recorder.
     */
    void record_grouped_rule(API::Recorder& recorder, const API::Buffer<std::uint32_t>& first,
                             const API::Buffer<std::uint32_t>& second,
                             const API::Buffer<std::uint32_t>& third)
    {
        std::uint32_t* a = first.data();
        std::uint32_t* b = second.data();
        std::uint32_t* c = third.data();

        auto rule = recorder.rule();
        recorder.kernel(WIDTH, API::Access(Reads(first), Writes(second)),
                        [a, b](std::size_t i) { b[i] = a[i] + 1u; });
        recorder.kernel(WIDTH, API::Access(Reads(second), Writes(third)),
                        [b, c](std::size_t i) { c[i] = b[i] * 2u; });
        recorder.kernel(WIDTH, API::Access(Reads(third), Writes(first)),
                        [c, a](std::size_t i) { a[i] = c[i]; });
    }

    /**
     * @brief Records three plain kernels and one rule of three nodes onto a fresh graph.
     *
     * The rule is the interesting half: its nodes share one registration index, which is what
     * makes the add() order and the report's numbering diverge.
     */
    Recorded record(Runtime& rt)
    {
        Recorded r{rt.graph(), rt.buffer<float>(WIDTH), rt.buffer<std::uint32_t>(WIDTH),
                   rt.buffer<std::uint32_t>(WIDTH), rt.buffer<std::uint32_t>(WIDTH), 0};
        for (std::size_t i = 0; i < WIDTH; ++i)
        {
            r.values[i] = 1.0f;
            r.first[i] = static_cast<std::uint32_t>(i % 7);
            r.second[i] = 0;
            r.third[i] = 0;
        }

        float* v = r.values.data();
        for (std::size_t k = 0; k < PLAIN_RULES; ++k)
            r.graph.add(Reads(r.values), Writes(r.values), WIDTH,
                        [v](std::size_t i) { v[i] = v[i] * 1.000001f; });

        record_grouped_rule(r.graph.recorder(), r.first, r.second, r.third);
        r.grouped = r.graph.last_rule();
        r.graph.set_profiling(true);
        return r;
    }
} // namespace

TEST(Integration_RuleRegistration, ARuleCarriesNoIndexUntilTheGraphCompiles)
{
    auto rt = Runtime::create();
    Recorded r = record(rt);

    ASSERT_EQ(r.graph.size(), PLAIN_RULES + 1u);
    ASSERT_EQ(r.graph.node_count(), PLAIN_RULES + RULE_NODES);
    for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
        EXPECT_EQ(r.graph.rule_registration(rule), API::Graph::no_registration)
            << "rule " << rule << " reported an index before the first run";
}

TEST(Integration_RuleRegistration, ARuleScopeTakesOneIndexOfItsOwn)
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

    ASSERT_EQ(r.grouped, PLAIN_RULES) << "the rule scope is the rule after the plain ones";
    const std::uint32_t shared = r.graph.rule_registration(r.grouped);
    EXPECT_NE(shared, API::Graph::no_registration);
    EXPECT_EQ(plain.count(shared), 0u) << "the rule must not reuse a plain add()'s index";
    EXPECT_EQ(r.graph.rule_registration(r.graph.size()), API::Graph::no_registration)
        << "a position past the last rule names no rule";

    std::size_t reported = 0;
    for (const Core::NodeEventRecord& event : report.node_events)
        reported += event.node_index == shared ? 1u : 0u;
    EXPECT_EQ(reported, RULE_NODES) << "every node of the rule scope must report its one index";
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

TEST(Integration_RuleRegistration, ARulesNodesReportOneIndexOnEveryStep)
{
    constexpr std::size_t STEPS = 4;

    auto rt = Runtime::create();
    Recorded r = record(rt);
    Core::RunReport report;
    r.graph.run(STEPS, report);
    ASSERT_TRUE(report.is_successful);
    ASSERT_EQ(report.node_events.size(), (PLAIN_RULES + RULE_NODES) * STEPS);

    const std::uint32_t shared = r.graph.rule_registration(r.grouped);
    std::vector<std::size_t> per_invocation(STEPS, 0);
    for (const Core::NodeEventRecord& event : report.node_events)
    {
        if (event.node_index != shared)
            continue;
        ASSERT_LT(event.invocation, STEPS);
        ++per_invocation[event.invocation];
    }
    for (std::size_t step = 0; step < STEPS; ++step)
        EXPECT_EQ(per_invocation[step], RULE_NODES)
            << "step " << step << ": every node of the rule must report the rule's index";
    EXPECT_EQ(r.graph.compile_count(), 1u);
}
