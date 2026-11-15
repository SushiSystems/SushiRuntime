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
 * @brief Tests that fold_observations() moves measured throughput into the profiles, smoothed and only on enough samples.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/profile_fold.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    constexpr OperationKind K_COMPUTE = OperationKinds::K_COMPUTE_BOUND;

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

    /** @brief Returns count copies of one observation. */
    std::vector<DispatchObservation> repeated(std::size_t count, const DispatchObservation& one)
    {
        return std::vector<DispatchObservation>(count, one);
    }

    /** @brief Returns the entry a fold gives: prior moved the smoothing weight of the way to median. */
    double moved(double prior, double median)
    {
        return prior + K_FOLD_SMOOTHING_WEIGHT * (median - prior);
    }

    double compute_throughput(const DeviceProfileSet& profiles, std::uint32_t ordinal)
    {
        return profiles.find(host_key(ordinal))->throughputs.at(K_COMPUTE);
    }

    TEST(Small_ProfileFold, AFoldMovesTheEntryTheSmoothingWeightOfTheWayToTheMedian)
    {
        const DeviceProfileSet folded = fold_observations(
            two_profiles(), repeated(K_MINIMUM_FOLD_SAMPLES, observation(1, K_COMPUTE, 4000, 1000, 1.25)));

        EXPECT_DOUBLE_EQ(compute_throughput(folded, 1), moved(8000.0, 16000.0));
    }

    TEST(Small_ProfileFold, RepeatedHorizonsConvergeToTheMedianAndPriceTheNodeAtTheMeasuredTime)
    {
        const std::vector<DispatchObservation> horizon =
            repeated(K_MINIMUM_FOLD_SAMPLES, observation(1, K_COMPUTE, 4000, 1000, 1.25));
        DeviceProfileSet profiles = two_profiles();
        double previous_distance = std::abs(compute_throughput(profiles, 1) - 16000.0);
        for (int index = 0; index < 60; ++index)
        {
            profiles = fold_observations(profiles, horizon);
            const double distance = std::abs(compute_throughput(profiles, 1) - 16000.0);
            EXPECT_LE(distance, previous_distance);
            previous_distance = distance;
        }

        EXPECT_NEAR(compute_throughput(profiles, 1), 16000.0, 1e-3);
        const NodeCost cost = observation(1, K_COMPUTE, 4000, 1000, 0.0).cost;
        EXPECT_NEAR(CostModel(profiles).node_time(host_key(1), cost).value(), 1.25, 1e-6);
    }

    TEST(Small_ProfileFold, AHorizonBelowTheMinimumMovesNothing)
    {
        const DeviceProfileSet original = two_profiles();
        const std::vector<DispatchObservation> few =
            repeated(K_MINIMUM_FOLD_SAMPLES - 1, observation(1, K_COMPUTE, 4000, 1000, 1.25));

        EXPECT_EQ(fold_observations(original, few), original);
    }

    TEST(Small_ProfileFold, TheMinimumCountsTheSamplesOfOneDeviceAndKind)
    {
        std::vector<DispatchObservation> mixed =
            repeated(K_MINIMUM_FOLD_SAMPLES - 1, observation(1, K_COMPUTE, 4000, 1000, 1.25));
        const std::vector<DispatchObservation> other =
            repeated(K_MINIMUM_FOLD_SAMPLES - 1, observation(2, K_COMPUTE, 4000, 1000, 1.25));
        mixed.insert(mixed.end(), other.begin(), other.end());
        mixed.push_back(observation(1, OperationKinds::K_GENERIC, 1000, 0, 0.5));

        EXPECT_EQ(fold_observations(two_profiles(), mixed), two_profiles());
    }

    TEST(Small_ProfileFold, OneOutlierHorizonMovesTheEntryByAtMostTheWeightOfTheDistance)
    {
        const double prior = 8000.0;
        const double outlier = 1.0e9;
        const DeviceProfileSet folded = fold_observations(
            two_profiles(), repeated(K_MINIMUM_FOLD_SAMPLES, observation(1, K_COMPUTE, 1000000000, 0, 1.0)));

        const double shift = compute_throughput(folded, 1) - prior;
        EXPECT_LE(shift, K_FOLD_SMOOTHING_WEIGHT * (outlier - prior) * (1.0 + 1e-12));
        EXPECT_LT(compute_throughput(folded, 1), outlier * K_FOLD_SMOOTHING_WEIGHT * 1.001);
    }

    TEST(Small_ProfileFold, AnEntryThatHoldsNoValueTakesTheMedianOutright)
    {
        DeviceProfileSet profiles;
        DeviceProfile profile = round_profile(1.0);
        profile.throughputs.clear();
        profile.base_throughput = 0.0;
        profiles.set(host_key(1), profile);

        const DeviceProfileSet folded = fold_observations(
            profiles, repeated(K_MINIMUM_FOLD_SAMPLES, observation(1, K_COMPUTE, 1000, 0, 0.5)));

        EXPECT_DOUBLE_EQ(compute_throughput(folded, 1), 2000.0);
    }

    TEST(Small_ProfileFold, TheEntryIsTheMedianOfTheSamplesAndTheLowerOneOfAnEvenCount)
    {
        std::vector<DispatchObservation> odd;
        std::vector<DispatchObservation> even;
        for (const double seconds : {0.1, 0.2, 0.4})
            for (int copy = 0; copy < 3; ++copy)
                odd.push_back(observation(1, K_COMPUTE, 1000, 0, seconds));
        for (const double seconds : {0.1, 0.2, 0.4, 0.5})
            for (int copy = 0; copy < 2; ++copy)
                even.push_back(observation(1, K_COMPUTE, 1000, 0, seconds));

        EXPECT_DOUBLE_EQ(compute_throughput(fold_observations(two_profiles(), odd), 1), moved(8000.0, 5000.0));
        EXPECT_DOUBLE_EQ(compute_throughput(fold_observations(two_profiles(), even), 1), moved(8000.0, 2500.0));
    }

    TEST(Small_ProfileFold, TheGenericKindSetsTheBaseThroughputAndAKindGetsAnEntryOfItsOwn)
    {
        const DeviceProfileSet folded = fold_observations(
            two_profiles(), repeated(K_MINIMUM_FOLD_SAMPLES, observation(2, OperationKinds::K_GENERIC, 1000, 0, 0.5)));
        const DeviceProfile& profile = *folded.find(host_key(2));

        EXPECT_DOUBLE_EQ(profile.base_throughput, moved(4000.0, 2000.0));
        EXPECT_EQ(profile.throughputs.size(), 1u);
        EXPECT_DOUBLE_EQ(profile.throughput_for(K_COMPUTE), 16000.0);
    }

    TEST(Small_ProfileFold, AFoldKeepsTheRestOfEveryProfile)
    {
        const DeviceProfileSet original = two_profiles();
        const DeviceProfileSet folded = fold_observations(
            original, repeated(K_MINIMUM_FOLD_SAMPLES, observation(1, K_COMPUTE, 1000, 0, 0.1)));
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
        std::vector<DispatchObservation> silent;
        const std::vector<DispatchObservation> kinds{observation(9, K_COMPUTE, 1000, 0, 0.1),
                                                     observation(1, K_COMPUTE, 1000, 0, 0.0),
                                                     observation(1, K_COMPUTE, 1000, 0, -1.0),
                                                     observation(1, K_COMPUTE, 1000, 0, not_a_number),
                                                     observation(1, K_COMPUTE, 0, 0, 0.1),
                                                     observation(1, K_COMPUTE, 1000, 1000, 1.0)};
        for (const DispatchObservation& one : kinds)
        {
            const std::vector<DispatchObservation> copies = repeated(K_MINIMUM_FOLD_SAMPLES, one);
            silent.insert(silent.end(), copies.begin(), copies.end());
        }

        const DeviceProfileSet folded = fold_observations(original, silent);

        EXPECT_EQ(folded, original);
        EXPECT_FALSE(folded.has_profile(host_key(9)));
    }

    TEST(Small_ProfileFold, TheOrderOfTheObservationsDoesNotMatter)
    {
        std::vector<DispatchObservation> observations;
        for (const double seconds : {0.1, 0.2, 0.4, 0.3})
            for (std::size_t copy = 0; copy < K_MINIMUM_FOLD_SAMPLES; ++copy)
                observations.push_back(observation(1, K_COMPUTE, 1000, 0, seconds));
        const std::vector<DispatchObservation> generic =
            repeated(K_MINIMUM_FOLD_SAMPLES, observation(2, OperationKinds::K_GENERIC, 1000, 0, 0.3));
        observations.insert(observations.end(), generic.begin(), generic.end());
        const DeviceProfileSet forward = fold_observations(two_profiles(), observations);
        std::reverse(observations.begin(), observations.end());
        const DeviceProfileSet backward = fold_observations(two_profiles(), observations);

        EXPECT_EQ(forward, backward);
    }

    TEST(Small_ProfileFold, TheSameInputsGiveTheSameDoubles)
    {
        const std::vector<DispatchObservation> horizon =
            repeated(K_MINIMUM_FOLD_SAMPLES + 3, observation(1, K_COMPUTE, 3000, 700, 0.9));
        DeviceProfileSet first = two_profiles();
        DeviceProfileSet second = two_profiles();
        for (int index = 0; index < 5; ++index)
        {
            first = fold_observations(first, horizon);
            second = fold_observations(second, horizon);
        }

        EXPECT_EQ(first, second);
    }
} // namespace
