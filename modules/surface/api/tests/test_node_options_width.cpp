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
 * @brief Tests that the width a node states through NodeOptions reaches its event records.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Checks that the report holds one event per expected width, in recording order. */
    void expect_widths(const Core::RunReport& report, const std::vector<std::uint32_t>& expected)
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
        API::Recorder& recorder = g.recorder();
        auto rule = recorder.rule();
        recorder.kernel(WIDE, API::Access(Reads(values), Writes(copies)),
                        [v, c](std::size_t i) { c[i] = v[i]; }, API::NodeOptions{WIDE});
        recorder.kernel(std::size_t{1}, API::Access(Reads(copies), Writes(first)),
                        [c, f](std::size_t) { f[0] = c[0]; }, API::NodeOptions{std::size_t{1}});
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
        API::Recorder& recorder = g.recorder();
        std::uint32_t* live =
            recorder.scratch<std::uint32_t>(1, out.device_index(), out.residency());
        const Core::ResourceRegion live_region{live, 0, sizeof(std::uint32_t)};

        auto rule = recorder.rule();
        recorder.resolve(std::array<LateCount, 1>{count}, out.device_index(),
                         API::Access(Reads(), Writes(live_region)),
                         [live](const std::array<std::size_t, 1>& value)
                         {
                             live[0] = static_cast<std::uint32_t>(value[0]);
                         },
                         API::NodeOptions{std::size_t{1}});
        recorder.kernel(API::DeviceSized{CAPACITY, live, nullptr},
                        API::Access(Reads(live_region), Writes(out)),
                        [o](std::size_t t) { o[t] = 1u; }, API::NodeOptions{CAPACITY});
    }

    const auto report = g.run();

    expect_widths(report, {1u, static_cast<std::uint32_t>(CAPACITY)});
    for (std::size_t t = 0; t < CAPACITY; ++t)
        EXPECT_EQ(out[t], 1u) << "item " << t;
    EXPECT_EQ(out[CAPACITY], 0u);
}
