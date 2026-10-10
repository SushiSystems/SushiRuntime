/**************************************************************************/
/* test_chain_layer.cpp                                                   */
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
 * @file test_chain_layer.cpp
 * @brief Tests that a chainable plan runs as an in-order chain, bit-identical to the scheduler.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N = 1024;

    /** @brief Holds every buffer the workload writes, copied out after the run. */
    struct Outputs
    {
        std::vector<float> field;          /**< The State's current field. */
        std::vector<std::uint32_t> acc;    /**< Written over a host-sized prefix each step. */
        std::vector<std::uint32_t> mix;    /**< Reads acc, so it orders after it. */
        std::vector<std::uint32_t> gated;  /**< Written only on the steps its when() allows. */
        Core::ExecutionLayer layer = Core::ExecutionLayer::Scheduler; /**< Layer of the last run. */
        std::size_t tasks = 0;             /**< total_tasks_executed summed over the runs. */
    };

    /**
     * @brief Builds the workload on @p rt, runs it @p runs times for @p steps each, and copies the
     *        results out.
     *
     * Host values change every step: the sized() count and the when() gate
     * both come from counters their providers advance. @p native_tail adds
     * a no-op host task, which keeps the plan off the chain.
     */
    Outputs run_workload(Runtime& rt, std::size_t runs, std::size_t steps, bool native_tail)
    {
        auto field = rt.state<float>(N);
        auto acc   = rt.buffer<std::uint32_t>(N);
        auto mix   = rt.buffer<std::uint32_t>(N);
        auto gated = rt.buffer<std::uint32_t>(N);
        for (std::size_t i = 0; i < N; ++i)
        {
            field[i] = static_cast<float>(i) * 0.5f;
            acc[i] = 0;
            mix[i] = 0;
            gated[i] = 0;
        }

        std::uint32_t* pa = acc.data();
        std::uint32_t* pm = mix.data();
        std::uint32_t* pg = gated.data();

        std::size_t size_tick = 0;
        std::size_t gate_tick = 0;

        auto g = rt.graph();
        g.add(field, [](std::size_t i, const float* cur, float* next)
        {
            next[i] = cur[i] * 0.75f + static_cast<float>(i % 7) * 0.125f;
        });
        g.add(API::sized([&size_tick] { return 1 + (size_tick++ * 37) % N; }),
              API::Reads{}, API::Writes(acc), N,
              [pa](std::size_t i) { pa[i] = pa[i] * 31u + static_cast<std::uint32_t>(i) + 1u; });
        g.add(API::Reads(acc), API::Writes(mix), N,
              [pa, pm](std::size_t i) { pm[i] = (pm[i] ^ (pa[i] * 2654435761u)) + 7u; });
        g.add(API::when([&gate_tick] { return (gate_tick++ % 3) != 0; }),
              API::Reads(mix), API::Writes(gated), N,
              [pm, pg](std::size_t i) { pg[i] = pg[i] * 17u + pm[i]; });
        if (native_tail)
            g.add_host([] {});

        Outputs out;
        for (std::size_t r = 0; r < runs; ++r)
        {
            const auto report = g.run(steps);
            EXPECT_TRUE(report.is_successful);
            out.layer = report.layer;
            out.tasks += report.total_tasks_executed;
        }

        out.field.assign(field.current(), field.current() + N);
        out.acc.assign(pa, pa + N);
        out.mix.assign(pm, pm + N);
        out.gated.assign(pg, pg + N);
        return out;
    }

    /**
     * @brief Compares two element vectors byte for byte.
     * @return True when both hold the same bytes.
     */
    template <class T>
    bool same_bytes(const std::vector<T>& a, const std::vector<T>& b)
    {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
    }

    /**
     * @brief Asserts two workload results are bit-identical, buffer by buffer.
     */
    void expect_identical(const Outputs& a, const Outputs& b)
    {
        EXPECT_TRUE(same_bytes(a.field, b.field)) << "State field differs";
        EXPECT_TRUE(same_bytes(a.acc, b.acc)) << "sized() buffer differs";
        EXPECT_TRUE(same_bytes(a.mix, b.mix)) << "dependent buffer differs";
        EXPECT_TRUE(same_bytes(a.gated, b.gated)) << "when() buffer differs";
        EXPECT_EQ(a.tasks, b.tasks);
    }
} // namespace

TEST(Integration_ChainLayer, ChainMatchesForcedSchedulerBitForBit)
{
    constexpr std::size_t RUNS  = 3;
    constexpr std::size_t STEPS = 40;

    auto chained_rt = Runtime::create();
    API::RuntimeConfig forced;
    forced.force_scheduler = true;
    auto scheduled_rt = Runtime::create(forced);

    const Outputs chained   = run_workload(chained_rt, RUNS, STEPS, false);
    const Outputs scheduled = run_workload(scheduled_rt, RUNS, STEPS, false);

    EXPECT_EQ(chained.layer, Core::ExecutionLayer::Chain)
        << "a plan of tracked kernels on one device must take the chain";
    EXPECT_EQ(scheduled.layer, Core::ExecutionLayer::Scheduler)
        << "force_scheduler must keep every plan on the scheduler";
    expect_identical(chained, scheduled);
}

TEST(Integration_ChainLayer, NonChainablePlanMatchesChainBitForBit)
{
    constexpr std::size_t RUNS  = 2;
    constexpr std::size_t STEPS = 64;

    auto rt = Runtime::create();

    const Outputs chained = run_workload(rt, RUNS, STEPS, false);
    const Outputs native  = run_workload(rt, RUNS, STEPS, true);

    EXPECT_EQ(chained.layer, Core::ExecutionLayer::Chain);
    EXPECT_EQ(native.layer, Core::ExecutionLayer::Scheduler)
        << "a native host task must keep the plan on the scheduler";

    EXPECT_TRUE(same_bytes(chained.field, native.field));
    EXPECT_TRUE(same_bytes(chained.acc, native.acc));
    EXPECT_TRUE(same_bytes(chained.mix, native.mix));
    EXPECT_TRUE(same_bytes(chained.gated, native.gated));
}

TEST(Integration_ChainLayer, RunUntilSeesEveryStepsResult)
{
    constexpr std::uint32_t TARGET = 9;

    auto rt = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1);
    counter[0] = 0;
    std::uint32_t* pc = counter.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(counter), 1, [pc](std::size_t) { pc[0] += 1u; });

    // The stop test reads a device result, so the chain must wait after every step.
    const auto report = g.run_until([&counter] { return counter[0] >= TARGET; });

    EXPECT_EQ(report.layer, Core::ExecutionLayer::Chain);
    EXPECT_EQ(report.total_tasks_executed, static_cast<std::size_t>(TARGET));
    EXPECT_EQ(counter[0], TARGET);
}

TEST(Integration_ChainLayer, ProfiledChainRecordsEveryDispatch)
{
    constexpr std::size_t STEPS = 12;

    auto rt = Runtime::create();
    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        a[i] = 0;
        b[i] = 0;
    }
    int* pa = a.data();
    int* pb = b.data();

    auto g = rt.graph();
    g.set_profiling(true);
    g.add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i) { pa[i] += 1; });
    g.add(API::Reads(a), API::Writes(b), N, [pa, pb](std::size_t i) { pb[i] += pa[i]; });

    const auto report = g.run(STEPS);

    EXPECT_EQ(report.layer, Core::ExecutionLayer::Chain)
        << "profiling must not move a chainable plan off layer 1";
    ASSERT_EQ(report.node_events.size(), 2 * STEPS);

    std::vector<std::size_t> per_step(STEPS, 0);
    for (const auto& rec : report.node_events)
    {
        ASSERT_LT(rec.invocation, STEPS);
        ++per_step[rec.invocation];
        EXPECT_LE(rec.start_ns, rec.end_ns);
    }
    for (std::size_t s = 0; s < STEPS; ++s)
        EXPECT_EQ(per_step[s], 2u) << "step " << s;

    ASSERT_EQ(report.node_timings.size(), 2u);
    for (const auto& t : report.node_timings)
        EXPECT_EQ(t.invocations, STEPS);

    // b[i] after k steps is 1 + 2 + ... + k.
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(b[i], static_cast<int>(STEPS * (STEPS + 1) / 2)) << "at i=" << i;
}
