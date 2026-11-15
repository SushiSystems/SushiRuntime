/**************************************************************************/
/* test_profile_update.cpp                                                */
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
 * @file test_profile_update.cpp
 * @brief Tests that a second run reads the throughput the first run measured, through the profile file.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/device_profile_file.hpp>
#include <SushiRuntime/device_profile/dispatch_observation.hpp>
#include <SushiRuntime/device_profile/profile_update.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Makes a profile file of two devices at a path of its own and removes the file when it goes. */
    class Small_ProfileUpdate : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                path_ = std::filesystem::temp_directory_path() / "sushiruntime_test_profile_update.txt";
                std::filesystem::remove(path_);
                DeviceProfileSet profiles;
                profiles.set(host_key(1), round_profile(1.0));
                profiles.set(host_key(2), round_profile(2.0));
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

    NodeCost compute_cost()
    {
        NodeCost cost;
        cost.operations = 4000;
        cost.read_bytes = 1000;
        cost.kind = OperationKinds::K_COMPUTE_BOUND;
        return cost;
    }

    TEST_F(Small_ProfileUpdate, ASecondRunReadsTheThroughputTheFirstRunMeasured)
    {
        const DeviceProfileSet before = DeviceProfileFile::read(path()).value();
        const double modelled_before = CostModel(before).node_time(host_key(1), compute_cost()).value();

        const std::vector<DispatchObservation> first_run{DispatchObservation{host_key(1), compute_cost(), 5.0}};
        ASSERT_TRUE(update_profile_file(path(), first_run).has_value());

        const DeviceProfileSet second_run = DeviceProfileFile::read(path()).value();
        const double modelled_after = CostModel(second_run).node_time(host_key(1), compute_cost()).value();
        EXPECT_DOUBLE_EQ(modelled_after, 5.0);
        EXPECT_NE(modelled_before, modelled_after);
        EXPECT_EQ(*second_run.find(host_key(2)), *before.find(host_key(2)));
    }

    TEST_F(Small_ProfileUpdate, ARunThatMeasuredNothingWritesTheSameProfiles)
    {
        const DeviceProfileSet before = DeviceProfileFile::read(path()).value();

        ASSERT_TRUE(update_profile_file(path(), {}).has_value());

        EXPECT_EQ(DeviceProfileFile::read(path()).value(), before);
    }

    TEST_F(Small_ProfileUpdate, ALaterRunFoldsOverTheEarlierOne)
    {
        ASSERT_TRUE(update_profile_file(path(), {DispatchObservation{host_key(1), compute_cost(), 5.0}}).has_value());
        ASSERT_TRUE(update_profile_file(path(), {DispatchObservation{host_key(1), compute_cost(), 2.0}}).has_value());

        const DeviceProfileSet profiles = DeviceProfileFile::read(path()).value();
        EXPECT_DOUBLE_EQ(CostModel(profiles).node_time(host_key(1), compute_cost()).value(), 2.0);
    }

    TEST_F(Small_ProfileUpdate, AMissingFileIsAnErrorAndCreatesNothing)
    {
        std::filesystem::remove(path());

        const Result<bool> result = update_profile_file(path(), {DispatchObservation{host_key(1), compute_cost(), 5.0}});

        EXPECT_FALSE(result.has_value());
        EXPECT_FALSE(std::filesystem::exists(path()));
    }
} // namespace
