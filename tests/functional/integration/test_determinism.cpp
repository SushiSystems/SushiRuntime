/**************************************************************************/
/* test_determinism.cpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// The determinism conformance suite: the same graph, on the same machine, must
// produce byte-identical output no matter how many workers ran it.
//
// This is the guarantee a rollback netcode or a replay file is built on, and it
// has two independent ways to break. The scheduler can break it — if the
// combination order of a reduction depended on which worker picked up which
// tile, a two-worker run and a sixteen-worker run would disagree in the low bits
// and nothing would report an error. The compiler can break it — fast-math
// licenses reassociation, so the same source folded in a different order between
// two builds, and FMA contraction rounds once where the source says twice.
// Unit_FixedOrderReduce pins the reduction's order at a fixed pool size; this
// suite varies the pool size itself, which is what BB-2's worker_count knob made
// expressible, and separately asserts the compiler contract that no amount of
// scheduling discipline can substitute for.
//
// Every comparison here is on the raw bytes. A tolerance-based check would pass
// on exactly the divergence that ruins a replay a thousand steps later.

#include <cstddef>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N     = 40000;
    constexpr std::size_t STEPS = 4;

    // One large value followed by many small ones: each small value vanishes
    // when added to the running total but survives when the small values are
    // summed together first. A sum over this input is a different number for a
    // different combination order, which is what gives a bit comparison teeth.
    // Without it the test would pass on a runtime that reassociated freely.
    float order_sensitive_value(std::size_t i)
    {
        return (i == 0) ? 1.0e8f : 1.0f;
    }

    // A second pattern, alternating in sign, so a divergence that happened to
    // cancel in the first reduction still shows up in the output.
    float alternating_value(std::size_t i)
    {
        const float magnitude = (i % 512 == 0) ? 1.0e7f : 0.5f;
        return (i % 2 == 0) ? magnitude : -magnitude;
    }

    // What one configuration produced, and the pool it actually ran on.
    struct Outcome
    {
        std::vector<float> output;  /**< The graph's output buffer, copied out. */
        float              first_total  = 0.0f;
        float              second_total = 0.0f;
        std::size_t        workers      = 0;
    };

    // Runs the same fixed workload on a runtime built for @p worker_count
    // workers (0 meaning the whole machine) and copies the results out. Two
    // independent reduction chains compete for the pool, so the worker that
    // folds a given tile genuinely varies with the pool size; a map then mixes
    // both totals into every output element, so any divergence in either chain
    // reaches the bytes being compared.
    Outcome run_workload(std::size_t worker_count)
    {
        API::RuntimeConfig config;
        config.worker_count = worker_count;
        config.rebalancer   = false;

        auto rt = Runtime::create({}, config);

        auto first   = rt.buffer<float>(N);
        auto second  = rt.buffer<float>(N);
        auto total_a = rt.buffer<float>(1);
        auto total_b = rt.buffer<float>(1);
        auto out     = rt.buffer<float>(N);

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
        g.add_reduce(first,  total_a, N, Sum<float>{});
        g.add_reduce(second, total_b, N, Sum<float>{});
        g.add(Reads(first, second, total_a, total_b), Writes(out), N,
              [pf, ps, pa, pb, po](std::size_t i)
              {
                  po[i] = pf[i] * pa[0] + ps[i] * pb[0];
              });

        g.run(STEPS);

        Outcome outcome;
        outcome.output.resize(N);
        for (std::size_t i = 0; i < N; ++i)
            outcome.output[i] = out[i];
        outcome.first_total  = total_a[0];
        outcome.second_total = total_b[0];
        outcome.workers      = rt.advanced().worker_pool().count;
        return outcome;
    }

    // Byte equality, not numeric equality: a 1-ulp difference must fail, and NaN
    // payloads must compare as the bits they are.
    bool bytes_equal(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() &&
               std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }

    // Index of the first differing element, for a failure message that points at
    // something instead of just saying "not equal".
    std::size_t first_difference(const std::vector<float>& a, const std::vector<float>& b)
    {
        const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
        for (std::size_t i = 0; i < n; ++i)
            if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0)
                return i;
        return n;
    }
}

// The compiler half of the contract, asserted where a consumer would feel it.
// This translation unit is not part of the library — it compiles against the
// installed-style interface — so a definition arriving here is evidence the FP
// contract travels with the target instead of stopping at the library edge.
// Restoring /fp:fast or -ffast-math turns this red at compile time, which is the
// property BB-3 asks the suite to have.
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

// Reassociation, caught at run time rather than by a macro — the check that
// still works on a compiler whose fast-math does not announce itself. The two
// expressions are the same three values in the same order, grouped differently;
// they are required to disagree. A compiler allowed to reassociate would rewrite
// one into the other and they would match.
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

// The suite's core claim. One worker, two workers and the whole machine must
// agree on every byte of the output.
TEST(Integration_Determinism, OutputIsByteEqualAcrossWorkerCounts)
{
    const Outcome single = run_workload(1);
    const Outcome pair   = run_workload(2);
    const Outcome full   = run_workload(0);   // 0 == the whole machine

    // Guard against a vacuous pass: if every configuration collapsed to the same
    // pool, nothing about the schedule actually varied.
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

    // The reduction results themselves, so a failure says which half moved.
    EXPECT_EQ(std::memcmp(&single.first_total, &pair.first_total, sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&single.first_total, &full.first_total, sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&single.second_total, &pair.second_total, sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&single.second_total, &full.second_total, sizeof(float)), 0);
}

// The same configuration, run again from scratch, must land on the same bytes.
// Distinct from the check above: that one varies the pool size, this one varies
// nothing at all and relies on the run-to-run scheduling jitter of an unchanged
// pool — which steal wins a race, which worker parks — to expose an order
// dependence.
TEST(Integration_Determinism, RepeatedConfigurationsAgreeByteForByte)
{
    const Outcome first  = run_workload(0);
    const Outcome second = run_workload(0);

    ASSERT_EQ(first.workers, second.workers);
    EXPECT_TRUE(bytes_equal(first.output, second.output))
        << "two identical runs diverged at element "
        << first_difference(first.output, second.output);
}

// A replayed graph must not drift: every step of a multi-step run recomputes the
// same reduction from the same input, so step 1 and step STEPS have to produce
// the same bytes. A drift here would mean the plan's per-step state leaked into
// the arithmetic.
TEST(Integration_Determinism, ReplayDoesNotDriftAcrossSteps)
{
    API::RuntimeConfig config;
    config.rebalancer = false;
    auto rt = Runtime::create({}, config);

    auto in    = rt.buffer<float>(N);
    auto total = rt.buffer<float>(1);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = order_sensitive_value(i);
    total[0] = 0.0f;

    auto g = rt.graph();
    g.add_reduce(in, total, N, Sum<float>{});

    g.run();
    const float after_one_step = total[0];

    total[0] = 0.0f;
    g.run(STEPS);

    EXPECT_EQ(std::memcmp(&after_one_step, &total[0], sizeof(float)), 0)
        << "a replayed reduction drifted: one step gave " << after_one_step
        << ", " << STEPS << " steps gave " << total[0];
    EXPECT_EQ(g.compile_count(), 1u) << "a replay must not recompile";
}
