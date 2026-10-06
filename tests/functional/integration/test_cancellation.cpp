/**************************************************************************/
/* test_cancellation.cpp                                                  */
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
 * @file test_cancellation.cpp
 * @brief Tests that cancel() from another thread stops run_until and leaves the next run alone.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <future>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/**
 * @brief A second thread cancels an unbounded run_until and it returns quickly.
 */
TEST(Integration_Cancellation, CancelStopsRunUntilFromAnotherThread)
{
    constexpr std::size_t N = 1024;
    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) field[i] = 0;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // run_until(false) never stops on its own — only cancel() can end it.
    auto fut = std::async(std::launch::async,
                          [&] { return g.run_until([] { return false; }); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    g.cancel();

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready)
        << "run_until did not return promptly after cancel()";

    auto report = fut.get();
    EXPECT_TRUE(report.cancelled);
    EXPECT_GT(report.total_tasks_executed, 0u) << "some steps should have run before cancel";
}

/**
 * @brief After a cancelled run, the next run starts clean and completes fully.
 */
TEST(Integration_Cancellation, NextRunIsNotCancelledAfterReset)
{
    constexpr std::size_t N = 256;
    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) field[i] = 0;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    auto fut = std::async(std::launch::async,
                          [&] { return g.run_until([] { return false; }); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    g.cancel();
    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(fut.get().cancelled);

    // A fresh bounded run must not inherit the previous cancellation.
    auto report = g.run(3);
    EXPECT_FALSE(report.cancelled);
    EXPECT_EQ(report.total_tasks_executed, 3u);
}
