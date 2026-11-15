/**************************************************************************/
/* test_partition_ledger.cpp                                              */
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

/**
 * @file test_partition_ledger.cpp
 * @brief Tests that PartitionLedger gives per-step figures and folds its dispatches into the profile file.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/device_profile_file.hpp>
#include <SushiRuntime/device_profile/profile_fold.hpp>
#include <SushiRuntime/partition_session/partition_ledger.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns a compute-bound cost of 4000 operations that reads 1000 bytes. */
    NodeCost compute_cost()
    {
        NodeCost cost;
        cost.operations = 4000;
        cost.read_bytes = 1000;
        cost.kind = OperationKinds::K_COMPUTE_BOUND;
        return cost;
    }

    /** @brief Returns a resolver that names host_key(1) for space 1 and no device for any other. */
    SpaceDeviceResolver first_device_at_space_one()
    {
        return [](MemorySpaceID space) -> std::optional<DeviceKey>
        {
            if (space == MemorySpaceID(1))
                return host_key(1);
            return std::nullopt;
        };
    }

    TEST(Small_PartitionLedger, FiguresAreGivenPerStep)
    {
        PartitionLedger ledger;
        ledger.add_steps(4, 2.0);
        ledger.add_dispatches({DispatchObservation{host_key(1), compute_cost(), 1.0},
                               DispatchObservation{host_key(1), compute_cost(), 3.0}});
        PathTallies copies;
        copies[{MemorySpaceID(1), TransferPathID::named("pcie")}] = PathTally{"pcie", 8, 800};
        copies[{MemorySpaceID(0), TransferPathID::named("pcie")}] = PathTally{"pcie", 4, 400};

        const PartitionMeasurement measured = ledger.measurement(copies, first_device_at_space_one());

        EXPECT_EQ(measured.step_seconds, 0.5);
        EXPECT_EQ(measured.device_seconds.at(host_key(1)), 1.0);
        ASSERT_EQ(measured.copies.size(), 1u);
        ASSERT_EQ(measured.copies.at(host_key(1)).size(), 1u);
        EXPECT_EQ(measured.copies.at(host_key(1))[0].path, "pcie");
        EXPECT_EQ(measured.copies.at(host_key(1))[0].copies, 2.0);
        EXPECT_EQ(measured.copies.at(host_key(1))[0].bytes, 200.0);
    }

    TEST(Small_PartitionLedger, TakeHorizonGivesTheDispatchesSinceTheLastTakeOnce)
    {
        PartitionLedger ledger;
        ledger.add_dispatches({DispatchObservation{host_key(1), NodeCost{}, 0.25}});
        static_cast<void>(ledger.take_horizon());
        ledger.add_dispatches({DispatchObservation{host_key(2), NodeCost{}, 0.5}});

        const std::vector<DispatchObservation> taken = ledger.take_horizon();

        ASSERT_EQ(taken.size(), 1u);
        EXPECT_EQ(taken[0].device, host_key(2));
        EXPECT_TRUE(ledger.take_horizon().empty());
    }

    TEST(Small_PartitionLedger, ALedgerWithNoStepMeasuresNothing)
    {
        PartitionLedger ledger;
        ledger.add_dispatches({DispatchObservation{host_key(1), compute_cost(), 1.0}});

        const PartitionMeasurement measured = ledger.measurement({}, first_device_at_space_one());

        EXPECT_EQ(measured.step_seconds, 0.0);
        EXPECT_TRUE(measured.device_seconds.empty());
    }

    /** @brief Makes a profile file of one device at a path of its own and removes the file when it goes. */
    class Medium_PartitionLedger : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                path_ = std::filesystem::temp_directory_path() / "sushiruntime_test_partition_ledger.txt";
                std::filesystem::remove(path_);
                DeviceProfileSet profiles;
                profiles.set(host_key(1), round_profile(1.0));
                ASSERT_TRUE(DeviceProfileFile::write(path_, profiles).has_value());
            }

            void TearDown() override
            {
                std::filesystem::remove(path_);
            }

            /** @brief Returns the path of the profile file. */
            [[nodiscard]] const std::filesystem::path& path() const noexcept
            {
                return path_;
            }

        private:
            std::filesystem::path path_;
    };

    TEST_F(Medium_PartitionLedger, ASecondRunReadsTheThroughputTheFirstRunFolded)
    {
        PartitionLedger first_run;
        first_run.add_steps(1, 5.0);
        const std::vector<DispatchObservation> horizon(K_MINIMUM_FOLD_SAMPLES,
                                                       DispatchObservation{host_key(1), compute_cost(), 5.0});
        first_run.add_dispatches(horizon);
        const DeviceProfileSet before = DeviceProfileFile::read(path()).value();

        const Result<bool> folded = first_run.fold_into(path());

        ASSERT_TRUE(folded.has_value());
        const DeviceProfileSet second_run = DeviceProfileFile::read(path()).value();
        const DeviceProfileSet expected = fold_observations(before, horizon);
        EXPECT_DOUBLE_EQ(CostModel(second_run).node_time(host_key(1), compute_cost()).value(),
                         CostModel(expected).node_time(host_key(1), compute_cost()).value());
    }

    TEST_F(Medium_PartitionLedger, ALedgerWithNothingUnfoldedLeavesTheFileAlone)
    {
        PartitionLedger ledger;
        ledger.add_dispatches(std::vector<DispatchObservation>(K_MINIMUM_FOLD_SAMPLES,
                                                               DispatchObservation{host_key(1), compute_cost(), 5.0}));
        ASSERT_TRUE(ledger.fold_into(path()).has_value());
        const DeviceProfileSet folded = DeviceProfileFile::read(path()).value();

        const Result<bool> again = ledger.fold_into(path());

        ASSERT_TRUE(again.has_value());
        EXPECT_FALSE(again.value());
        EXPECT_EQ(DeviceProfileFile::read(path()).value(), folded);
    }
} // namespace
