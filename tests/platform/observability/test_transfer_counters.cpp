/**************************************************************************/
/* test_transfer_counters.cpp                                             */
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
 * @file test_transfer_counters.cpp
 * @brief Tests TransferCounters' starting values, per-counter adds and concurrent totals.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/transfer_counters.hpp>

using SushiRuntime::Internal::Observability::TransferCounters;

namespace
{
    /** @brief Bytes each thread adds to the device-to-host counter per call. */
    constexpr std::size_t K_DEVICE_TO_HOST_STEP = 3;

    /** @brief Bytes each thread adds to the host-to-device counter per call. */
    constexpr std::size_t K_HOST_TO_DEVICE_STEP = 5;

    /** @brief Number of threads the concurrency test starts. */
    constexpr int K_THREAD_COUNT = 8;

    /** @brief Number of add pairs each thread performs. */
    constexpr int K_ADDS_PER_THREAD = 1000;
} // namespace

TEST(Medium_TransferCounters, BothCountersStartAtZero)
{
    TransferCounters counters;

    EXPECT_EQ(counters.device_to_host(), 0u);
    EXPECT_EQ(counters.host_to_device(), 0u);
}

TEST(Medium_TransferCounters, AddDeviceToHostRaisesOnlyItsCounter)
{
    TransferCounters counters;

    counters.add_device_to_host(64);

    EXPECT_EQ(counters.device_to_host(), 64u);
    EXPECT_EQ(counters.host_to_device(), 0u);
}

TEST(Medium_TransferCounters, AddHostToDeviceRaisesOnlyItsCounter)
{
    TransferCounters counters;

    counters.add_host_to_device(128);

    EXPECT_EQ(counters.host_to_device(), 128u);
    EXPECT_EQ(counters.device_to_host(), 0u);
}

TEST(Medium_TransferCounters, RepeatedAddsAccumulate)
{
    TransferCounters counters;

    counters.add_device_to_host(10);
    counters.add_device_to_host(32);
    counters.add_host_to_device(7);
    counters.add_host_to_device(1);

    EXPECT_EQ(counters.device_to_host(), 42u);
    EXPECT_EQ(counters.host_to_device(), 8u);
}

TEST(Medium_TransferCounters, ConcurrentAddsGiveExactTotals)
{
    // Every thread adds the fixed steps to both counters K_ADDS_PER_THREAD times.
    TransferCounters counters;
    std::vector<std::thread> workers;
    workers.reserve(K_THREAD_COUNT);

    for (int t = 0; t < K_THREAD_COUNT; ++t)
    {
        workers.emplace_back([&counters]
        {
            for (int i = 0; i < K_ADDS_PER_THREAD; ++i)
            {
                counters.add_device_to_host(K_DEVICE_TO_HOST_STEP);
                counters.add_host_to_device(K_HOST_TO_DEVICE_STEP);
            }
        });
    }
    for (auto& w : workers)
        w.join();

    const std::size_t adds = static_cast<std::size_t>(K_THREAD_COUNT) * K_ADDS_PER_THREAD;
    EXPECT_EQ(counters.device_to_host(), adds * K_DEVICE_TO_HOST_STEP);
    EXPECT_EQ(counters.host_to_device(), adds * K_HOST_TO_DEVICE_STEP);
}
