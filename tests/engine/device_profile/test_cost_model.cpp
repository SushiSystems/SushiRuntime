/**************************************************************************/
/* test_cost_model.cpp                                                  */
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
 * @file test_cost_model.cpp
 * @brief Tests that CostModel prices nodes and copies from the profiles by the stated formula.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <limits>

#include <SushiRuntime/device_profile/cost_model.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    CostModel model()
    {
        DeviceProfileSet profiles;
        profiles.set(host_key(1), round_profile(1.0));
        profiles.set(host_key(2), round_profile(2.0));
        return CostModel(profiles);
    }

    TEST(Small_CostModel, ANodeTakesItsOperationsOverThroughputPlusItsBytesOverBandwidth)
    {
        NodeCost cost;
        cost.operations = 4000;
        cost.read_bytes = 1000;
        cost.written_bytes = 500;

        EXPECT_DOUBLE_EQ(*model().node_time(host_key(1), cost), 4000.0 / 2000.0 + 1500.0 / 1000.0);
        EXPECT_DOUBLE_EQ(*model().node_time(host_key(2), cost), 4000.0 / 4000.0 + 1500.0 / 2000.0);
    }

    TEST(Small_CostModel, AKindWithAnEntryUsesItsThroughputAndOtherKindsTheBase)
    {
        NodeCost bound;
        bound.operations = 8000;
        bound.kind = OperationKinds::K_COMPUTE_BOUND;
        NodeCost other = bound;
        other.kind = "gemm"_kind;

        EXPECT_DOUBLE_EQ(*model().node_time(host_key(1), bound), 1.0);
        EXPECT_DOUBLE_EQ(*model().node_time(host_key(1), other), 4.0);
    }

    TEST(Small_CostModel, ADeviceWithNoProfileHasNoTimeAndNoProfile)
    {
        EXPECT_FALSE(model().has_profile(host_key(7)));
        EXPECT_FALSE(model().node_time(host_key(7), NodeCost{}).has_value());
        EXPECT_TRUE(model().has_profile(host_key(1)));
    }

    TEST(Small_CostModel, ADeviceThatCannotDoTheWorkTakesInfiniteTime)
    {
        DeviceProfileSet profiles;
        profiles.set(host_key(1), DeviceProfile{});
        NodeCost cost;
        cost.operations = 1;

        EXPECT_EQ(*CostModel(profiles).node_time(host_key(1), cost),
                  std::numeric_limits<double>::infinity());
        EXPECT_EQ(*CostModel(profiles).node_time(host_key(1), NodeCost{}), 0.0);
    }

    TEST(Small_CostModel, ACopyTakesTheLatencyPlusItsBytesOverTheBandwidthOfThePath)
    {
        const TransferPathID pcie = TransferPathID::named("pcie");
        EXPECT_DOUBLE_EQ(*model().transfer_time(host_key(1), host_key(2), pcie, 1000), 0.25 + 1000.0 / 500.0);
        EXPECT_DOUBLE_EQ(*model().transfer_time(host_key(2), host_key(1), pcie, 1000), 0.25 + 1000.0 / 1000.0);
    }

    TEST(Small_CostModel, ACopyOverAnUnknownPathHasNoTime)
    {
        const TransferPathID pcie = TransferPathID::named("pcie");
        EXPECT_FALSE(model().transfer_time(host_key(1), host_key(2), TransferPathID::named("nvlink"), 1000).has_value());
        EXPECT_FALSE(model().transfer_time(host_key(1), host_key(2), TransferPathID(), 1000).has_value());
        EXPECT_FALSE(model().transfer_time(host_key(8), host_key(9), pcie, 1000).has_value());
    }

    TEST(Small_CostModel, APathIsPricedByItsIdentityAndNeverByTheNameItCarries)
    {
        DeviceProfile profile = round_profile(1.0);
        profile.paths[TransferPathID::named("one")] = PathProfile{500.0, 0.25, "another"};
        DeviceProfileSet profiles;
        profiles.set(host_key(1), profile);
        const CostModel priced(profiles);

        EXPECT_TRUE(priced.transfer_time(host_key(1), host_key(2), TransferPathID::named("one"), 10).has_value());
        EXPECT_FALSE(priced.transfer_time(host_key(1), host_key(2), TransferPathID::named("another"), 10).has_value());
    }

    TEST(Small_CostModel, ThePathOfTheDestinationServesWhenTheSourceHasNone)
    {
        DeviceProfileSet profiles;
        profiles.set(host_key(2), round_profile(1.0));
        const CostModel only_destination(profiles);

        EXPECT_TRUE(only_destination.transfer_time(host_key(9), host_key(2), TransferPathID::named("pcie"), 10).has_value());
    }
} // namespace
