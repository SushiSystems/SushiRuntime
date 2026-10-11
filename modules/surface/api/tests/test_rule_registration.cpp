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

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t K_WIDTH = 64;
    constexpr std::size_t K_PLAIN_RULES = 3;
    constexpr std::size_t K_RULE_NODES = 3;

    /** @brief Holds one graph, the buffers it names and the position of the grouped rule. */
    struct Recorded
    {
        Graph graph;                  /**< The graph under test. */
        Buffer<float> values;         /**< The plain kernels' target. */
        Buffer<std::uint32_t> first;  /**< The rule's input and final output. */
        Buffer<std::uint32_t> second; /**< What the rule's first node writes. */
        Buffer<std::uint32_t> third;  /**< What the rule's second node writes. */
        std::size_t grouped = 0;           /**< Position of the three-node rule. */
    };

    /**
     * @brief Records K_RULE_NODES kernels in one rule scope, each reading what the last wrote.
     * @param first,second,third Hold K_WIDTH words each; must outlive the recorder.
     */
    void record_grouped_rule(Recorder& recorder, const Buffer<std::uint32_t>& first,
                             const Buffer<std::uint32_t>& second,
                             const Buffer<std::uint32_t>& third)
    {
        std::uint32_t* a = first.data();
        std::uint32_t* b = second.data();
        std::uint32_t* c = third.data();

        auto rule = recorder.rule();
        recorder.kernel(K_WIDTH, Access(Reads(first), Writes(second)),
                        [a, b](std::size_t i) { b[i] = a[i] + 1u; });
        recorder.kernel(K_WIDTH, Access(Reads(second), Writes(third)),
                        [b, c](std::size_t i) { c[i] = b[i] * 2u; });
        recorder.kernel(K_WIDTH, Access(Reads(third), Writes(first)),
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
        Recorded r{rt.graph(),
                   rt.buffer<float>(K_WIDTH).value(),
                   rt.buffer<std::uint32_t>(K_WIDTH).value(),
                   rt.buffer<std::uint32_t>(K_WIDTH).value(),
                   rt.buffer<std::uint32_t>(K_WIDTH).value(),
                   0};
        for (std::size_t i = 0; i < K_WIDTH; ++i)
        {
            r.values[i] = 1.0f;
            r.first[i] = static_cast<std::uint32_t>(i % 7);
            r.second[i] = 0;
            r.third[i] = 0;
        }

        float* v = r.values.data();
        for (std::size_t k = 0; k < K_PLAIN_RULES; ++k)
            r.graph.add(K_WIDTH, Reads(r.values), Writes(r.values),
                        [v](std::size_t i) { v[i] = v[i] * 1.000001f; });

        record_grouped_rule(r.graph.recorder(), r.first, r.second, r.third);
        r.grouped = r.graph.last_rule();
        r.graph.set_profiling(true);
        return r;
    }
} // namespace

TEST(Integration_RuleRegistration, ARuleCarriesNoIndexUntilTheGraphCompiles)
{
    auto rt = Runtime::create().value();
    Recorded r = record(rt);

    ASSERT_EQ(r.graph.size(), K_PLAIN_RULES + 1u);
    ASSERT_EQ(r.graph.node_count(), K_PLAIN_RULES + K_RULE_NODES);
    for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
        EXPECT_EQ(r.graph.rule_registration(rule), Graph::K_NO_REGISTRATION)
            << "rule " << rule << " reported an index before the first run";
}

TEST(Integration_RuleRegistration, ARuleScopeTakesOneIndexOfItsOwn)
{
    auto rt = Runtime::create().value();
    Recorded r = record(rt);
    Internal::Observability::RunReport report;
    r.graph.run(1, report);
    ASSERT_TRUE(report.is_successful);

    std::set<std::uint32_t> plain;
    for (std::size_t rule = 0; rule < K_PLAIN_RULES; ++rule)
    {
        const std::uint32_t index = r.graph.rule_registration(rule);
        EXPECT_NE(index, Graph::K_NO_REGISTRATION);
        plain.insert(index);
    }
    EXPECT_EQ(plain.size(), K_PLAIN_RULES) << "a plain add() must take an index of its own";

    ASSERT_EQ(r.grouped, K_PLAIN_RULES) << "the rule scope is the rule after the plain ones";
    const std::uint32_t shared = r.graph.rule_registration(r.grouped);
    EXPECT_NE(shared, Graph::K_NO_REGISTRATION);
    EXPECT_EQ(plain.count(shared), 0u) << "the rule must not reuse a plain add()'s index";
    EXPECT_EQ(r.graph.rule_registration(r.graph.size()), Graph::K_NO_REGISTRATION)
        << "a position past the last rule names no rule";

    std::size_t reported = 0;
    for (const Internal::Observability::NodeEventRecord& event : report.node_events)
        reported += event.node_index == shared ? 1u : 0u;
    EXPECT_EQ(reported, K_RULE_NODES) << "every node of the rule scope must report its one index";
}

TEST(Integration_RuleRegistration, EveryNodeEventNamesARecordedRule)
{
    auto rt = Runtime::create().value();
    Recorded r = record(rt);
    Internal::Observability::RunReport report;
    r.graph.run(1, report);
    ASSERT_TRUE(report.is_successful);
    ASSERT_FALSE(report.node_events.empty()) << "profiling was requested, so events must exist";

    std::set<std::uint32_t> recorded;
    for (std::size_t rule = 0; rule < r.graph.size(); ++rule)
        recorded.insert(r.graph.rule_registration(rule));

    for (const Internal::Observability::NodeEventRecord& event : report.node_events)
        EXPECT_EQ(recorded.count(event.node_index), 1u)
            << "node event " << event.node_index << " matches no rule the caller recorded";
}

TEST(Integration_RuleRegistration, ARulesNodesReportOneIndexOnEveryStep)
{
    constexpr std::size_t STEPS = 4;

    auto rt = Runtime::create().value();
    Recorded r = record(rt);
    Internal::Observability::RunReport report;
    r.graph.run(STEPS, report);
    ASSERT_TRUE(report.is_successful);
    ASSERT_EQ(report.node_events.size(), (K_PLAIN_RULES + K_RULE_NODES) * STEPS);

    const std::uint32_t shared = r.graph.rule_registration(r.grouped);
    std::vector<std::size_t> per_invocation(STEPS, 0);
    for (const Internal::Observability::NodeEventRecord& event : report.node_events)
    {
        if (event.node_index != shared)
            continue;
        ASSERT_LT(event.invocation, STEPS);
        ++per_invocation[event.invocation];
    }
    for (std::size_t step = 0; step < STEPS; ++step)
        EXPECT_EQ(per_invocation[step], K_RULE_NODES)
            << "step " << step << ": every node of the rule must report the rule's index";
    EXPECT_EQ(r.graph.compile_count(), 1u);
}

/** @brief Tests that the position a Recorder verb returns is the one rule_registration() takes. */
TEST(Integration_RuleRegistration, AVerbReturnsThePositionOfTheRuleItRecorded)
{
    constexpr std::uint32_t K_SCALE_MARK = 11;
    constexpr std::uint32_t K_COPY_MARK = 22;

    auto rt = Runtime::create().value();
    auto words = rt.buffer<std::uint32_t>(K_WIDTH).value();
    auto copies = rt.buffer<std::uint32_t>(K_WIDTH).value();
    std::uint32_t* w = words.data();
    std::uint32_t* c = copies.data();
    for (std::size_t i = 0; i < K_WIDTH; ++i)
        w[i] = static_cast<std::uint32_t>(i);

    // Each node states a width no other node has, so its event is found by that width.
    NodeOptions scale_options;
    scale_options.width = K_SCALE_MARK;
    NodeOptions copy_options;
    copy_options.width = K_COPY_MARK;

    auto graph = rt.graph();
    graph.set_profiling(true);
    Recorder& recorder = graph.recorder();

    const std::size_t scale = recorder.kernel(K_WIDTH, Access(Reads(words), Writes(words)),
                                              [w](std::size_t i) { w[i] *= 2u; },
                                              scale_options);
    EXPECT_EQ(scale, graph.last_rule());

    std::size_t copy = 0;
    std::size_t idle = 0;
    {
        auto rule = recorder.rule();
        copy = recorder.kernel(K_WIDTH, Access(Reads(words), Writes(copies)),
                               [w, c](std::size_t i) { c[i] = w[i]; }, copy_options);
        idle = recorder.host(Access(Reads(copies), Writes()), [] {});
    }
    EXPECT_EQ(copy, graph.last_rule());
    EXPECT_EQ(idle, copy) << "the nodes of one scope belong to one rule";
    EXPECT_NE(scale, copy);

    const auto report = graph.run();

    std::uint32_t scale_index = Graph::K_NO_REGISTRATION;
    std::uint32_t copy_index = Graph::K_NO_REGISTRATION;
    for (const Internal::Observability::NodeEventRecord& event : report.node_events)
    {
        if (event.launched_width == K_SCALE_MARK)
            scale_index = event.node_index;
        if (event.launched_width == K_COPY_MARK)
            copy_index = event.node_index;
    }

    ASSERT_NE(scale_index, Graph::K_NO_REGISTRATION);
    ASSERT_NE(copy_index, Graph::K_NO_REGISTRATION);
    EXPECT_EQ(graph.rule_registration(scale), scale_index);
    EXPECT_EQ(graph.rule_registration(copy), copy_index);
    EXPECT_EQ(copies[K_WIDTH - 1], static_cast<std::uint32_t>(K_WIDTH - 1) * 2u);
}
