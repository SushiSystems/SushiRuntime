/**************************************************************************/
/* test_barrier.cpp                                                       */
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
 * @file test_barrier.cpp
 * @brief Tests Barrier: no thread passes before the last arrives, and it serves again.
 * @see tests/foundation/thread_barrier/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/thread_barrier/barrier.hpp>

using namespace SushiRuntime::Test;

/** @brief Holds the threads that meet at the barrier. */
constexpr std::size_t K_THREADS = 4;

/** @brief Tests that every thread arrived before any passed, over two rounds. */
TEST(Medium_Barrier, NoThreadPassesBeforeTheLastArrives)
{
    Barrier barrier(K_THREADS);
    std::atomic<std::size_t> arrived{0};
    std::atomic<bool> early{false};
    std::vector<std::thread> threads;
    for (std::size_t index = 0; index < K_THREADS; ++index)
    {
        threads.emplace_back([&]
        {
            for (int round = 1; round <= 2; ++round)
            {
                arrived.fetch_add(1);
                barrier.arrive_and_wait();
                if (arrived.load() < K_THREADS * static_cast<std::size_t>(round))
                    early.store(true);
                barrier.arrive_and_wait();
            }
        });
    }
    for (std::thread& thread : threads)
        thread.join();
    EXPECT_FALSE(early.load()) << "a thread passed before every thread arrived";
    EXPECT_EQ(arrived.load(), 2 * K_THREADS);
}

/** @brief Tests that a barrier of one never waits. */
TEST(Medium_Barrier, ABarrierOfOneNeverWaits)
{
    Barrier barrier(1);
    barrier.arrive_and_wait();
    barrier.arrive_and_wait();
    SUCCEED();
}
