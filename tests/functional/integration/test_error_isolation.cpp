/****************************************************************/
/* test_error_isolation.cpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_error_isolation.cpp
 * @brief Tests that a task exception aborts only its own run and leaves the Runtime reusable.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <stdexcept>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Fills a fresh graph that writes 42 into every element of @p p. */
    void run_good_graph(Runtime& rt, int* p, std::size_t n)
    {
        auto g = rt.graph();
        g.add_untracked(n, [p](std::size_t i) { p[i] = 42; });
        g.run();
    }
}

/**
 * @brief A throwing kernel fails its run but leaves the Runtime reusable.
 */
TEST(Integration_ErrorIsolation, ThrowingKernelLeavesRuntimeReusable)
{
    constexpr std::size_t N = 64;
    auto rt = Runtime::create();

    // execute() must report the throw from the first graph's command group; see README.md.
    {
        auto g = rt.graph();
        g.add_untracked([](sycl::handler&) { throw std::runtime_error("boom"); });
        EXPECT_THROW(g.run(), SushiException);
    }

    // Second graph on the SAME Runtime must run cleanly — proof the pool survived.
    auto buf = rt.buffer<int>(N);
    int* p   = buf.data();
    for (std::size_t i = 0; i < N; ++i) p[i] = 0;

    EXPECT_NO_THROW(run_good_graph(rt, p, N));
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], 42) << "at i=" << i;
}

/**
 * @brief The pool survives repeated failures interleaved with good runs.
 */
TEST(Integration_ErrorIsolation, RuntimeSurvivesRepeatedFailures)
{
    constexpr std::size_t N = 32;
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p   = buf.data();

    for (int round = 0; round < 5; ++round)
    {
        {
            auto bad = rt.graph();
            bad.add_untracked([](sycl::handler&) { throw std::runtime_error("boom"); });
            EXPECT_THROW(bad.run(), SushiException) << "round " << round;
        }

        for (std::size_t i = 0; i < N; ++i) p[i] = 0;
        EXPECT_NO_THROW(run_good_graph(rt, p, N)) << "round " << round;
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], 42) << "round " << round << " at i=" << i;
    }
}
