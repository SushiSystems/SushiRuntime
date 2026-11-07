/**************************************************************************/
/* test_host_runtime_config.cpp                                           */
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
 * @file test_host_runtime_config.cpp
 * @brief Tests the host runtime HostRuntimeConfig builds, against the runtime contract and its own promises.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "support/runtime_contract.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Makes the runtime the contract runs on: a host runtime of its own per case. */
    struct HostRuntimeMaker
    {
        /** @brief Fills the slot with a host runtime on its first call and returns it. */
        static Runtime& runtime(std::optional<Runtime>& slot)
        {
            if (!slot)
                slot.emplace(Runtime::create_host().value());
            return *slot;
        }
    };

    /** @brief Returns the order in which a host runtime of one seed runs independent host nodes. */
    std::vector<int> order_of_seed(std::uint64_t seed)
    {
        HostRuntimeConfig config;
        config.executor = std::make_shared<SeededExecutor>(seed);
        Runtime runtime = Runtime::create_host(config).value();
        std::vector<int> order;
        auto graph = runtime.graph();
        for (int node = 0; node < 6; ++node)
            graph.add_host([&order, node]() { order.push_back(node); });
        static_cast<void>(graph.run().value());
        return order;
    }
} // namespace

namespace TestSupport
{
    /** @brief Runs the runtime contract on a host runtime. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_HostRuntime, RuntimeContract, ::HostRuntimeMaker, );
} // namespace TestSupport

/** @brief Tests that a host runtime of no device is refused with InvalidArgument. */
TEST(Small_HostRuntime, RefusesADeviceCountOfZero)
{
    HostRuntimeConfig config;
    config.device_count = 0;

    auto made = Runtime::create_host(config);

    ASSERT_FALSE(made.has_value());
    EXPECT_EQ(made.error().code, ErrorCode::InvalidArgument);
}

/** @brief Tests that a host runtime reports the device count it was given in its topology. */
TEST(Small_HostRuntime, ReportsItsDeviceCountInItsTopology)
{
    HostRuntimeConfig config;
    config.device_count = 3;

    Runtime runtime = Runtime::create_host(config).value();

    EXPECT_EQ(runtime.topology().devices.size(), 3u);
}

/** @brief Tests that a host runtime names its machine from the config, not from the host. */
TEST(Small_HostRuntime, NamesItsMachineFromTheConfig)
{
    HostRuntimeConfig config;
    config.node_name = "bench-7";

    Runtime runtime = Runtime::create_host(config).value();

    EXPECT_EQ(runtime.topology().name, "bench-7");
}

/** @brief Tests that two host runtimes of one seed run independent host nodes in one order. */
TEST(Small_HostRuntime, OneSeedReplaysTheOrderOfIndependentNodes)
{
    EXPECT_EQ(order_of_seed(21), order_of_seed(21));
}

/** @brief Tests that a launch over an Extent runs each index once, the origin added. */
TEST(Small_HostRuntime, ALaunchOverAnExtentRunsEachIndexOnceFromItsOrigin)
{
    Runtime runtime = Runtime::create_host().value();
    auto buffer = runtime.buffer<int>(12).value();
    int* cells = buffer.data();
    for (std::size_t i = 0; i < 12; ++i)
        cells[i] = 0;
    auto graph = runtime.graph();
    graph.add(Extent<2>({2, 3}, {1, 1}), Reads{}, Writes(buffer),
              [cells](sycl::id<2> index) { cells[index[0] * 4 + index[1]] += 1; });

    ASSERT_TRUE(graph.run().has_value());

    EXPECT_EQ(buffer[5], 1);
    EXPECT_EQ(buffer[0], 0);
}
