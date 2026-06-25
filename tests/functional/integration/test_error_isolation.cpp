/**************************************************************************/
/* test_error_isolation.cpp                                               */
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

// A task exception must abort only its own run, not kill the worker pool. The
// run that threw surfaces the error to the caller; a fresh graph on the same
// Runtime must still run and produce correct results afterward. Before the fix a
// single throw latched shutdown_requested and left the pool permanently dead.

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
        g.add(n, [p](std::size_t i) { p[i] = 42; });
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

    // First graph: the command-group function throws, so execute() must report
    // the error to the caller rather than hang.
    {
        auto g = rt.graph();
        g.add([](sycl::handler&) { throw std::runtime_error("boom"); });
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
            bad.add([](sycl::handler&) { throw std::runtime_error("boom"); });
            EXPECT_THROW(bad.run(), SushiException) << "round " << round;
        }

        for (std::size_t i = 0; i < N; ++i) p[i] = 0;
        EXPECT_NO_THROW(run_good_graph(rt, p, N)) << "round " << round;
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], 42) << "round " << round << " at i=" << i;
    }
}
