/**************************************************************************/
/* test_run_placement_policy.cpp                                          */
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
 * @file test_run_placement_policy.cpp
 * @brief Tests RunPlacementPolicy: a caller's class answers through the port and may keep state.
 * @see tests/foundation/placement/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/placement/run_placement_policy.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Places a run on the host below a launched-item threshold and counts its calls. */
    class ThresholdPolicy final : public RunPlacementPolicy
    {
        public:
            /** @brief Places on the host every run of fewer than @p threshold launched items. */
            explicit ThresholdPolicy(std::uint64_t threshold) noexcept : threshold_(threshold)
            {

            }

            /** @brief Returns Host below the threshold, Device at or above it. */
            [[nodiscard]] RunPlacement place(const RunDescription& run) override
            {
                ++calls_;
                return run.launched_items < threshold_ ? RunPlacement::Host : RunPlacement::Device;
            }

            /** @brief Returns how many runs the policy placed. */
            [[nodiscard]] std::size_t calls() const noexcept { return calls_; }

        private:
            std::uint64_t threshold_; /**< Launched items from which a run goes to the device. */
            std::size_t calls_ = 0;   /**< Runs placed so far. */
    };
} // namespace

/** @brief Tests that a caller's policy answers through the port, run by run, and keeps its state. */
TEST(Small_RunPlacementPolicy, ACallersPolicyAnswersThroughThePort)
{
    auto threshold = std::make_shared<ThresholdPolicy>(100);
    RunPlacementPolicy& policy = *threshold;
    RunDescription small;
    small.launched_items = 99;
    RunDescription large;
    large.launched_items = 100;

    EXPECT_EQ(policy.place(small), RunPlacement::Host);
    EXPECT_EQ(policy.place(large), RunPlacement::Device);
    EXPECT_EQ(threshold->calls(), 2u);
}
