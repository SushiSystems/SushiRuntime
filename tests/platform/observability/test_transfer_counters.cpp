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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_transfer_counters.cpp
 * @brief Tests TransferCounters' per-counter adds, accumulation and concurrent totals.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <functional>
#include <string>
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

    /** @brief One add, and the totals the two counters must read after it. */
    struct AddRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief Makes the add on a fresh set of counters. */
        std::function<void(TransferCounters&)> add;
        /** @brief The device-to-host total afterwards. */
        std::size_t device_to_host;
        /** @brief The host-to-device total afterwards. */
        std::size_t host_to_device;
    };

    /** @brief Runs @p threads threads that each add the fixed steps to both counters @p adds_per_thread times. */
    void add_from_threads(TransferCounters& counters, int threads, int adds_per_thread)
    {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(threads));
        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back([&counters, adds_per_thread]
            {
                for (int i = 0; i < adds_per_thread; ++i)
                {
                    counters.add_device_to_host(K_DEVICE_TO_HOST_STEP);
                    counters.add_host_to_device(K_HOST_TO_DEVICE_STEP);
                }
            });
        }
        for (auto& w : workers)
            w.join();
    }
} // namespace

class Small_TransferCountersAdd : public ::testing::TestWithParam<AddRow>
{
};

/** @brief Tests that one add raises only its own counter. */
TEST_P(Small_TransferCountersAdd, RaisesOnlyItsCounter)
{
    TransferCounters counters;

    GetParam().add(counters);

    EXPECT_EQ(counters.device_to_host(), GetParam().device_to_host);
    EXPECT_EQ(counters.host_to_device(), GetParam().host_to_device);
}

INSTANTIATE_TEST_SUITE_P(Directions, Small_TransferCountersAdd,
                         ::testing::Values(AddRow{"DeviceToHost", [](TransferCounters& counters) { counters.add_device_to_host(64); }, 64, 0},
                                           AddRow{"HostToDevice", [](TransferCounters& counters) { counters.add_host_to_device(128); }, 0, 128}),
                         [](const ::testing::TestParamInfo<AddRow>& info) { return std::string(info.param.name); });

/** @brief Tests that repeated adds accumulate in each counter. */
TEST(Small_TransferCounters, RepeatedAddsAccumulate)
{
    TransferCounters counters;

    counters.add_device_to_host(10);
    counters.add_device_to_host(32);
    counters.add_host_to_device(7);
    counters.add_host_to_device(1);

    EXPECT_EQ(counters.device_to_host(), 42u);
    EXPECT_EQ(counters.host_to_device(), 8u);
}

/** @brief Tests that no add is lost when several threads add to both counters at once. */
TEST(Medium_TransferCounters, ConcurrentAddsGiveExactTotals)
{
    TransferCounters counters;

    add_from_threads(counters, K_THREAD_COUNT, K_ADDS_PER_THREAD);

    const std::size_t adds = static_cast<std::size_t>(K_THREAD_COUNT) * K_ADDS_PER_THREAD;
    EXPECT_EQ(counters.device_to_host(), adds * K_DEVICE_TO_HOST_STEP);
    EXPECT_EQ(counters.host_to_device(), adds * K_HOST_TO_DEVICE_STEP);
}
