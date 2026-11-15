/**************************************************************************/
/* test_profile_fold.cpp                                                  */
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
 * @file test_profile_fold.cpp
 * @brief Tests that fold_observations() folds measured throughput into the profiles and nothing else.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <vector>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/profile_fold.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    DeviceProfileSet two_profiles()
    {
        DeviceProfileSet profiles;
        profiles.set(host_key(1), round_profile(1.0));
        profiles.set(host_key(2), round_profile(2.0));
        return profiles;
    }

    DispatchObservation observation(std::uint32_t ordinal, OperationKind kind, std::uint64_t operations,
                                    std::uint64_t moved_bytes, double seconds)
    {
        NodeCost cost;
        cost.operations = operations;
        cost.read_bytes = moved_bytes;
        cost.kind = kind;
        return DispatchObservation{host_key(ordinal), cost, seconds};
    }

    TEST(Small_ProfileFold, AFoldedProfilePricesTheObservedNodeAtTheMeasuredTime)
    {
        const DeviceProfileSet folded = fold_observations(
            two_profiles(), {observation(1, OperationKinds::K_COMPUTE_BOUND, 4000, 1000, 1.25)});

        EXPECT_DOUBLE_EQ(folded.find(host_key(1))->throughputs.at(OperationKinds::K_COMPUTE_BOUND), 16000.0);
        const NodeCost cost = observation(1, OperationKinds::K_COMPUTE_BOUND, 4000, 1000, 0.0).cost;
        EXPECT_DOUBLE_EQ(CostModel(folded).node_time(host_key(1), cost).value(), 1.25);
    }

    TEST(Small_ProfileFold, TheEntryIsTheMedianOfTheSamplesAndTheLowerOneOfAnEvenCount)
    {
        const DeviceProfileSet odd = fold_observations(
            two_profiles(), {observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.1),
                             observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.2),
                             observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.4)});
        const DeviceProfileSet even = fold_observations(
            two_profiles(), {observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.1),
                             observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.2),
                             observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.4),
                             observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.5)});

        EXPECT_DOUBLE_EQ(odd.find(host_key(1))->throughputs.at(OperationKinds::K_COMPUTE_BOUND), 5000.0);
        EXPECT_DOUBLE_EQ(even.find(host_key(1))->throughputs.at(OperationKinds::K_COMPUTE_BOUND), 2500.0);
    }

    TEST(Small_ProfileFold, TheGenericKindSetsTheBaseThroughputAndAKindGetsAnEntryOfItsOwn)
    {
        const DeviceProfileSet folded = fold_observations(
            two_profiles(), {observation(2, OperationKinds::K_GENERIC, 1000, 0, 0.5)});
        const DeviceProfile& profile = *folded.find(host_key(2));

        EXPECT_DOUBLE_EQ(profile.base_throughput, 2000.0);
        EXPECT_EQ(profile.throughputs.size(), 1u);
        EXPECT_DOUBLE_EQ(profile.throughput_for(OperationKinds::K_COMPUTE_BOUND), 16000.0);
    }

    TEST(Small_ProfileFold, AFoldKeepsTheRestOfEveryProfile)
    {
        const DeviceProfileSet original = two_profiles();
        const DeviceProfileSet folded = fold_observations(
            original, {observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.1)});
        const DeviceProfile& before = *original.find(host_key(1));
        const DeviceProfile& after = *folded.find(host_key(1));

        EXPECT_EQ(after.capacity_bytes, before.capacity_bytes);
        EXPECT_EQ(after.reserve_bytes, before.reserve_bytes);
        EXPECT_EQ(after.memory_bandwidth, before.memory_bandwidth);
        EXPECT_EQ(after.base_throughput, before.base_throughput);
        EXPECT_EQ(after.paths, before.paths);
        EXPECT_EQ(*folded.find(host_key(2)), *original.find(host_key(2)));
    }

    TEST(Small_ProfileFold, AnObservationThatSaysNothingChangesNothing)
    {
        const DeviceProfileSet original = two_profiles();
        const double not_a_number = std::numeric_limits<double>::quiet_NaN();
        const std::vector<DispatchObservation> silent{
            observation(9, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.1),
            observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.0),
            observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, -1.0),
            observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, not_a_number),
            observation(1, OperationKinds::K_COMPUTE_BOUND, 0, 0, 0.1),
            observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 1000, 1.0)};

        const DeviceProfileSet folded = fold_observations(original, silent);

        EXPECT_EQ(folded, original);
        EXPECT_FALSE(folded.has_profile(host_key(9)));
    }

    TEST(Small_ProfileFold, TheOrderOfTheObservationsDoesNotMatter)
    {
        std::vector<DispatchObservation> observations{observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.1),
                                                      observation(2, OperationKinds::K_GENERIC, 1000, 0, 0.3),
                                                      observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.2),
                                                      observation(1, OperationKinds::K_COMPUTE_BOUND, 1000, 0, 0.4)};
        const DeviceProfileSet forward = fold_observations(two_profiles(), observations);
        std::reverse(observations.begin(), observations.end());
        const DeviceProfileSet backward = fold_observations(two_profiles(), observations);

        EXPECT_EQ(forward, backward);
    }
} // namespace
