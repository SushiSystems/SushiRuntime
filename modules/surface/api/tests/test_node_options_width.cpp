/**************************************************************************/
/* test_node_options_width.cpp                                            */
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
 * @file test_node_options_width.cpp
 * @brief Tests that what a node states through NodeOptions reaches the report of a run.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Checks that the report holds one event per expected width, in recording order. */
    void expect_widths(const Internal::Observability::RunReport& report,
                       const std::vector<std::uint32_t>& expected)
    {
        ASSERT_EQ(report.node_events.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(report.node_events[i].launched_width, expected[i]) << "node " << i;
            EXPECT_EQ(report.node_events[i].live_width, expected[i]) << "node " << i;
        }
    }
} // namespace

/** @brief Tests that each node of a rule reports the width it stated, not a buffer's size. */
TEST(Integration_NodeOptionsWidth, EachNodeOfARuleReportsItsStatedWidth)
{
    constexpr std::size_t WIDE = 600;

    // The buffers are oversized by different amounts to expose a buffer width; see README.md.
    auto rt     = Runtime::create();
    auto values = rt.buffer<std::uint32_t>(WIDE + 9);
    auto copies = rt.buffer<std::uint32_t>(WIDE + 8);
    auto first  = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < WIDE; ++i)
        values[i] = static_cast<std::uint32_t>(i) + 1u;
    const std::uint32_t* v = values.data();
    std::uint32_t* c = copies.data();
    std::uint32_t* f = first.data();

    auto g = rt.graph();
    g.set_profiling(true);
    {
        Recorder& recorder = g.recorder();
        auto rule = recorder.rule();
        recorder.kernel(WIDE, Access(Reads(values), Writes(copies)),
                        [v, c](std::size_t i) { c[i] = v[i]; },
                        NodeOptions{WIDE});
        recorder.kernel(std::size_t{1}, Access(Reads(copies), Writes(first)),
                        [c, f](std::size_t) { f[0] = c[0]; },
                        NodeOptions{std::size_t{1}});
    }

    const auto report = g.run();

    expect_widths(report, {static_cast<std::uint32_t>(WIDE), 1u});
    EXPECT_EQ(first[0], 1u);
}

/** @brief Tests that a resolver and a device-sized node report the widths they stated. */
TEST(Integration_NodeOptionsWidth, ResolverAndDeviceSizedNodeReportTheirStatedWidths)
{
    constexpr std::size_t CAPACITY = 128;

    auto rt  = Runtime::create();
    auto out = rt.buffer<std::uint32_t>(CAPACITY + 8);
    for (std::size_t i = 0; i < CAPACITY + 8; ++i)
        out[i] = 0;
    std::uint32_t* o = out.data();

    const LateCount count = LateCount::fixed(CAPACITY);

    auto g = rt.graph();
    g.set_profiling(true);
    {
        Recorder& recorder = g.recorder();
        std::uint32_t* live =
            recorder.scratch<std::uint32_t>(1, out.device_index(), out.residency());
        const Internal::Memory::ResourceRegion live_region{live, 0, sizeof(std::uint32_t)};

        auto rule = recorder.rule();
        recorder.resolve(std::array<LateCount, 1>{count}, out.device_index(),
                         Access(Reads(), Writes(live_region)),
                         [live](const std::array<std::size_t, 1>& value)
                         {
                             live[0] = static_cast<std::uint32_t>(value[0]);
                         },
                         NodeOptions{std::size_t{1}});
        recorder.kernel(DeviceSized{CAPACITY, live, nullptr},
                        Access(Reads(live_region), Writes(out)),
                        [o](std::size_t t) { o[t] = 1u; },
                        NodeOptions{CAPACITY});
    }

    const auto report = g.run();

    expect_widths(report, {1u, static_cast<std::uint32_t>(CAPACITY)});
    for (std::size_t t = 0; t < CAPACITY; ++t)
        EXPECT_EQ(out[t], 1u) << "item " << t;
    EXPECT_EQ(out[CAPACITY], 0u);
}

/** @brief Tests that a profiled run reports the name and the operation a node stated. */
TEST(Integration_NodeOptionsWidth, ANodeReportsTheNameAndTheOperationItStated)
{
    constexpr std::size_t WIDE = 64;

    auto rt     = Runtime::create();
    auto values = rt.buffer<std::uint32_t>(WIDE);
    for (std::size_t i = 0; i < WIDE; ++i)
        values[i] = static_cast<std::uint32_t>(i);
    std::uint32_t* v = values.data();

    auto g = rt.graph();
    g.set_profiling(true);
    {
        // The label and the parameters end with this scope, before the graph runs.
        const std::string label = "scale by three";
        const std::array<std::uint64_t, 2> parameters{3u, WIDE};
        g.recorder().kernel(WIDE, Access(Reads(values), Writes(values)),
                            [v](std::size_t i) { v[i] *= 3u; },
                            NodeOptions{WIDE, label, "options.scale"_op, parameters});
    }

    const auto report = g.run();

    ASSERT_EQ(report.node_timings.size(), 1u);
    EXPECT_EQ(report.node_timings[0].name, "scale by three");
    EXPECT_EQ(report.node_timings[0].operation, "options.scale"_op.value);
    expect_widths(report, {static_cast<std::uint32_t>(WIDE)});
    EXPECT_EQ(values[WIDE - 1], static_cast<std::uint32_t>(WIDE - 1) * 3u);
}

/** @brief Tests that a node's own name is reported in place of the name of its rule. */
TEST(Integration_NodeOptionsWidth, ANodeNameReplacesTheNameOfItsRule)
{
    constexpr std::size_t WIDE = 32;

    auto rt     = Runtime::create();
    auto first  = rt.buffer<std::uint32_t>(WIDE);
    auto second = rt.buffer<std::uint32_t>(WIDE);
    std::uint32_t* a = first.data();
    std::uint32_t* b = second.data();

    NodeOptions named;
    named.name = "own name";

    auto g = rt.graph();
    g.set_profiling(true);
    g.add(WIDE, Reads(), Writes(first), [a](std::size_t i) { a[i] = 1u; }, named)
        .name("rule name");
    g.add(WIDE, Reads(first), Writes(second), [a, b](std::size_t i) { b[i] = a[i]; })
        .name("rule name");

    const auto report = g.run();

    ASSERT_EQ(report.node_timings.size(), 2u);
    EXPECT_EQ(report.node_timings[0].name, "own name");
    EXPECT_EQ(report.node_timings[0].operation, 0u);
    EXPECT_EQ(report.node_timings[1].name, "rule name");
}

/** @brief Tests that more parameters than a node carries are refused and record nothing. */
TEST(Integration_NodeOptionsWidth, MoreParametersThanANodeCarriesAreRefused)
{
    constexpr std::size_t WIDE = 8;

    auto rt     = Runtime::create();
    auto values = rt.buffer<std::uint32_t>(WIDE);
    std::uint32_t* v = values.data();

    const std::array<std::uint64_t, 9> parameters{1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};
    NodeOptions options;
    options.parameters = parameters;

    auto g = rt.graph();
    ErrorCode refused = ErrorCode::Ok;
    try
    {
        g.add(WIDE, Reads(), Writes(values), [v](std::size_t i) { v[i] = 0u; }, options);
    }
    catch (const SushiException& e)
    {
        refused = e.error().code;
    }

    EXPECT_EQ(refused, ErrorCode::InvalidGraph);
    EXPECT_EQ(g.size(), 0u);
}
