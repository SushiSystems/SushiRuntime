/**************************************************************************/
/* test_determinism.cpp                                                   */
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
 * @file test_determinism.cpp
 * @brief Tests that the same graph produces byte-identical output for every worker count.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N     = 40000;
    constexpr std::size_t K_STEPS = 4;

    /**
     * @brief Returns one large value followed by many small ones, so a sum depends on its
     *        combination order.
     */
    float order_sensitive_value(std::size_t i)
    {
        return (i == 0) ? 1.0e8f : 1.0f;
    }

    /** @brief Returns a second pattern that alternates in sign. */
    float alternating_value(std::size_t i)
    {
        const float magnitude = (i % 512 == 0) ? 1.0e7f : 0.5f;
        return (i % 2 == 0) ? magnitude : -magnitude;
    }

    constexpr std::size_t K_TILE  = 256;
    constexpr std::size_t TILES = (N + K_TILE - 1) / K_TILE;

    /**
     * @brief Records one rule of two nodes that totals N values in an order fixed by K_TILE.
     *
     * One work-item per tile adds its values left to right into recorder scratch, then one
     * work-item adds the partials left to right into total[0].
     * @param input Holds N values; must outlive the recorder.
     */
    void record_tiled_total(Recorder& recorder, const Buffer<float>& input,
                            const Buffer<float>& total)
    {
        float* partials =
            recorder.scratch<float>(TILES, input.device_index(), input.residency()).value();
        const BufferRegion partial_region{partials, 0, TILES * sizeof(float)};
        const float* in = input.data();
        float* out = total.data();

        auto rule = recorder.rule();
        recorder.kernel(TILES, Access(Reads(input), Writes(partial_region)),
                        [in, partials](std::size_t t)
                        {
                            const std::size_t begin = t * K_TILE;
                            const std::size_t end = (begin + K_TILE < N) ? begin + K_TILE : N;
                            float sum = in[begin];
                            for (std::size_t i = begin + 1; i < end; ++i)
                                sum = sum + in[i];
                            partials[t] = sum;
                        });
        recorder.kernel(std::size_t{1}, Access(Reads(partial_region), Writes(total)),
                        [partials, out](std::size_t)
                        {
                            float sum = partials[0];
                            for (std::size_t t = 1; t < TILES; ++t)
                                sum = sum + partials[t];
                            out[0] = sum;
                        });
    }

    // What one configuration produced, and the pool it actually ran on.
    struct Outcome
    {
        std::vector<float> output;  /**< The graph's output buffer, copied out. */
        float              first_total  = 0.0f;
        float              second_total = 0.0f;
        std::size_t        workers      = 0;
    };

    /**
     * @brief Runs the fixed workload on a runtime built for the given worker count and copies the
     *        results out.
     * @param rebalancer Whether the background rebalancer runs during the workload.
     */
    Outcome run_workload(std::size_t worker_count, bool rebalancer = false)
    {
        RuntimeConfig config;
        config.worker_count = worker_count;
        config.rebalancer   = rebalancer;

        auto rt = Runtime::create(config).value();

        auto first   = rt.buffer<float>(N).value();
        auto second  = rt.buffer<float>(N).value();
        auto total_a = rt.buffer<float>(1).value();
        auto total_b = rt.buffer<float>(1).value();
        auto out     = rt.buffer<float>(N).value();

        for (std::size_t i = 0; i < N; ++i)
        {
            first[i]  = order_sensitive_value(i);
            second[i] = alternating_value(i);
            out[i]    = 0.0f;
        }
        total_a[0] = 0.0f;
        total_b[0] = 0.0f;

        const float* pf = first.data();
        const float* ps = second.data();
        const float* pa = total_a.data();
        const float* pb = total_b.data();
        float*       po = out.data();

        auto g = rt.graph();
        record_tiled_total(g.recorder(), first, total_a);
        record_tiled_total(g.recorder(), second, total_b);
        g.add(N, Reads(first, second, total_a, total_b), Writes(out),
              [pf, ps, pa, pb, po](std::size_t i)
              {
                  po[i] = pf[i] * pa[0] + ps[i] * pb[0];
              });

        g.run(K_STEPS).value();

        Outcome outcome;
        outcome.output.resize(N);
        for (std::size_t i = 0; i < N; ++i)
            outcome.output[i] = out[i];
        outcome.first_total  = total_a[0];
        outcome.second_total = total_b[0];
        outcome.workers      = rt.advanced().worker_pool().count;
        return outcome;
    }

    /** @brief Returns whether two float vectors are equal byte for byte. */
    bool bytes_equal(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() &&
               std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }

    /** @brief Returns the index of the first differing element. */
    std::size_t first_difference(const std::vector<float>& a, const std::vector<float>& b)
    {
        const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
        for (std::size_t i = 0; i < n; ++i)
            if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0)
                return i;
        return n;
    }
}

/** @brief Tests that the floating-point compile contract reaches a consumer translation unit. */
TEST(Integration_Determinism, TheFloatingPointContractReachesConsumerCode)
{
#ifndef SR_DETERMINISTIC_FP
    GTEST_SKIP() << "built with SR_DETERMINISTIC_FP=OFF; byte-equal replay is "
                    "not claimed for this configuration";
#else
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
    constexpr bool fast_math_enabled = true;
#else
    constexpr bool fast_math_enabled = false;
#endif
    EXPECT_FALSE(fast_math_enabled)
        << "fast-math is enabled: reassociation is licensed, so the same source "
           "can fold to different bits between builds and replay cannot be "
           "byte-equal";
#endif
}

/** @brief Tests that the compiler does not reassociate host arithmetic. */
TEST(Integration_Determinism, HostArithmeticIsNotReassociated)
{
    volatile double big   = 1.0e16;
    volatile double minus = -1.0e16;
    volatile double one   = 1.0;

    const double left  = (big + minus) + one;  // 0.0 + 1.0 == 1.0
    const double right = big + (minus + one);  // 1e16 + -1e16 == 0.0

    EXPECT_NE(left, right)
        << "the compiler reassociated a floating-point sum: the same source can "
           "fold to different bits between builds";
    EXPECT_DOUBLE_EQ(left, 1.0);
    EXPECT_DOUBLE_EQ(right, 0.0);
}

/** @brief Tests that one worker, two workers and the whole machine agree on every output byte. */
TEST(Integration_Determinism, OutputIsByteEqualAcrossWorkerCounts)
{
    const Outcome single = run_workload(1);
    const Outcome pair   = run_workload(2);
    const Outcome full   = run_workload(0);   // 0 == the whole machine

    // With fewer than two workers every configuration is the same pool; see README.md.
    if (full.workers < 2)
        GTEST_SKIP() << "single-core machine (pool = " << full.workers
                     << "); worker-count variation is not observable here";

    EXPECT_EQ(single.workers, 1u) << "worker_count = 1 did not produce a one-worker pool";
    EXPECT_GT(full.workers, single.workers)
        << "the default pool is no wider than the one-worker pool; the comparison "
           "below would not be varying anything";

    EXPECT_TRUE(bytes_equal(single.output, pair.output))
        << "1 vs 2 workers diverged at element " << first_difference(single.output, pair.output);
    EXPECT_TRUE(bytes_equal(single.output, full.output))
        << "1 vs " << full.workers << " workers diverged at element "
        << first_difference(single.output, full.output);

    // The two totals themselves, so a failure says which half moved.
    EXPECT_EQ(std::memcmp(&single.first_total, &pair.first_total, sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&single.first_total, &full.first_total, sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&single.second_total, &pair.second_total, sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&single.second_total, &full.second_total, sizeof(float)), 0);
}

/** @brief Tests that the same configuration run again from scratch lands on the same bytes. */
TEST(Integration_Determinism, RepeatedConfigurationsAgreeByteForByte)
{
    const Outcome first  = run_workload(0);
    const Outcome second = run_workload(0);

    ASSERT_EQ(first.workers, second.workers);
    EXPECT_TRUE(bytes_equal(first.output, second.output))
        << "two identical runs diverged at element "
        << first_difference(first.output, second.output);
}

/** @brief Tests that a run with the background rebalancer on lands on the same bytes. */
TEST(Integration_Determinism, TheRebalancerDoesNotChangeTheBytes)
{
    const Outcome quiet  = run_workload(0, false);
    const Outcome moving = run_workload(0, true);

    ASSERT_EQ(quiet.workers, moving.workers);
    EXPECT_TRUE(bytes_equal(quiet.output, moving.output))
        << "the rebalancer changed element " << first_difference(quiet.output, moving.output);
}

/** @brief Tests that the first and the last step of a multi-step run produce the same bytes. */
TEST(Integration_Determinism, ReplayDoesNotDriftAcrossSteps)
{
    RuntimeConfig config;
    config.rebalancer = false;
    auto rt = Runtime::create(config).value();

    auto in    = rt.buffer<float>(N).value();
    auto total = rt.buffer<float>(1).value();
    for (std::size_t i = 0; i < N; ++i)
        in[i] = order_sensitive_value(i);
    total[0] = 0.0f;

    auto g = rt.graph();
    record_tiled_total(g.recorder(), in, total);

    g.run().value();
    const float after_one_step = total[0];

    total[0] = 0.0f;
    g.run(K_STEPS).value();

    EXPECT_EQ(std::memcmp(&after_one_step, &total[0], sizeof(float)), 0)
        << "a replayed rule drifted: one step gave " << after_one_step
        << ", " << K_STEPS << " steps gave " << total[0];
    EXPECT_EQ(g.compile_count(), 1u) << "a replay must not recompile";
}
