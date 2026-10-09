/**************************************************************************/
/* test_error_isolation.cpp                                               */
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
 * @file test_error_isolation.cpp
 * @brief Tests that a task exception aborts only its own run and leaves the Runtime reusable.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <stdexcept>
#include <string>
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
 * @brief A throwing node is named, with its registration index, in the error try_run() returns.
 */
TEST(Integration_ErrorIsolation, ThrowingNodeIsReportedByNameAndRegistrationIndex)
{
    auto rt = Runtime::create();
    auto g  = rt.graph();
    g.add_host([] {}).name_last("healthy");
    g.add_host([] { throw std::runtime_error("boom"); }).name_last("faulty");

    const auto result = g.try_run();
    ASSERT_FALSE(result);

    const std::string& message = result.error().message;
    EXPECT_NE(message.find("node 'faulty' (registration index 1)"), std::string::npos) << message;
    EXPECT_NE(message.find("boom"), std::string::npos) << message;
    EXPECT_EQ(message.find("healthy"), std::string::npos) << message;
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
