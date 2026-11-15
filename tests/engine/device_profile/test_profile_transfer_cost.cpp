/**************************************************************************/
/* test_profile_transfer_cost.cpp                                       */
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
 * @file test_profile_transfer_cost.cpp
 * @brief Tests that the planner's cost function follows the cost model when one is loaded and the uniform cost when none is.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include <SushiRuntime/device_profile/profile_transfer_cost.hpp>

#include "support/fake_profile_probe.hpp"
#include "support/fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    std::optional<DeviceKey> by_space(MemorySpaceID space)
    {
        if (space.value() > 2)
            return std::nullopt;
        return host_key(space.value());
    }

    std::shared_ptr<const CostModel> loaded_model()
    {
        DeviceProfile profile = round_profile(1.0);
        profile.set_path("fake", PathProfile{500.0, 0.25, {}});
        DeviceProfileSet profiles;
        profiles.set(host_key(1), profile);
        profiles.set(host_key(2), profile);
        return std::make_shared<const CostModel>(profiles);
    }

    TEST(Small_ProfileTransferCost, WithNoModelTheCostIsTheUniformOne)
    {
        const TransferCost cost = select_transfer_cost(nullptr, by_space);
        FakeTransferPath path;

        EXPECT_EQ(cost(path, MemorySpaceID(1), MemorySpaceID(2), 128), 128.0);
    }

    TEST(Small_ProfileTransferCost, WithAModelTheCostIsTheModelledSecondsOfTheCopy)
    {
        const TransferCost cost = select_transfer_cost(loaded_model(), by_space);
        FakeTransferPath path;

        EXPECT_DOUBLE_EQ(cost(path, MemorySpaceID(1), MemorySpaceID(2), 1000), 0.25 + 1000.0 / 500.0);
    }

    TEST(Small_ProfileTransferCost, ACopyTheModelCannotPriceCostsInfinity)
    {
        const TransferCost cost = select_transfer_cost(loaded_model(), by_space);
        FakeTransferPath path;
        ClosedTransferPath unmodelled;

        EXPECT_EQ(cost(path, MemorySpaceID(1), MemorySpaceID(9), 10), std::numeric_limits<double>::infinity());
        EXPECT_EQ(cost(unmodelled, MemorySpaceID(1), MemorySpaceID(2), 10),
                  std::numeric_limits<double>::infinity());
    }

    TEST(Small_ProfileTransferCost, ThePriceFollowsThePathIdentityNotTheNameTheEntryCarries)
    {
        DeviceProfile profile = round_profile(1.0);
        FakeTransferPath path;
        ClosedTransferPath other;
        profile.paths[path.id()] = PathProfile{500.0, 0.25, std::string(other.name())};
        DeviceProfileSet profiles;
        profiles.set(host_key(1), profile);
        profiles.set(host_key(2), profile);
        const TransferCost cost =
            select_transfer_cost(std::make_shared<const CostModel>(profiles), by_space);

        EXPECT_DOUBLE_EQ(cost(path, MemorySpaceID(1), MemorySpaceID(2), 1000), 0.25 + 1000.0 / 500.0);
        EXPECT_EQ(cost(other, MemorySpaceID(1), MemorySpaceID(2), 1000), std::numeric_limits<double>::infinity());
    }

    /** @brief Returns a model whose two devices measured their host links and no path named for ClosedTransferPath. */
    std::shared_ptr<const CostModel> host_linked_model()
    {
        DeviceProfile first = round_profile(1.0);
        first.host_to_device = PathProfile{400.0, 0.125};
        first.device_to_host = PathProfile{200.0, 0.0625};
        DeviceProfile second = round_profile(1.0);
        second.host_to_device = PathProfile{800.0, 0.25};
        second.device_to_host = PathProfile{100.0, 0.5};
        DeviceProfileSet profiles;
        profiles.set(host_key(1), first);
        profiles.set(host_key(2), second);
        return std::make_shared<const CostModel>(profiles);
    }

    TEST(Small_ProfileTransferCost, ACopyThroughTheHostIsTheSumOfTheTwoHostLinks)
    {
        const TransferCost cost = select_transfer_cost(host_linked_model(), by_space);
        ClosedTransferPath staged;

        const double seconds = cost(staged, MemorySpaceID(1), MemorySpaceID(2), 1000);

        EXPECT_TRUE(std::isfinite(seconds));
        EXPECT_DOUBLE_EQ(seconds, (0.0625 + 1000.0 / 200.0) + (0.25 + 1000.0 / 800.0));
    }

    TEST(Small_ProfileTransferCost, ACopyToOrFromTheHostIsPricedByTheDeviceHostLink)
    {
        const TransferCost cost = select_transfer_cost(host_linked_model(), by_space);
        ClosedTransferPath path;
        const MemorySpaceID host(9);

        EXPECT_DOUBLE_EQ(cost(path, host, MemorySpaceID(1), 1000), 0.125 + 1000.0 / 400.0);
        EXPECT_DOUBLE_EQ(cost(path, MemorySpaceID(2), host, 1000), 0.5 + 1000.0 / 100.0);
    }

    TEST(Small_ProfileTransferCost, ACopyBetweenTwoHostSideSpacesIsUnmeasured)
    {
        const TransferCost cost = select_transfer_cost(host_linked_model(), by_space);
        ClosedTransferPath path;

        EXPECT_EQ(cost(path, MemorySpaceID(8), MemorySpaceID(9), 10), std::numeric_limits<double>::infinity());
    }
} // namespace
