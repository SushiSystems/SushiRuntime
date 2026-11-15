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
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <limits>
#include <memory>

#include <SushiRuntime/coherence/profile_transfer_cost.hpp>

#include "fake_profile_probe.hpp"
#include "fake_transfer_path.hpp"

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
        profile.paths["fake"] = PathProfile{500.0, 0.25};
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
} // namespace
