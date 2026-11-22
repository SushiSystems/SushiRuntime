/**************************************************************************/
/* test_recorded_steps.cpp                                                */
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
 * @file test_recorded_steps.cpp
 * @brief Tests that RecordedSteps is owned through its base and never copied or moved.
 * @see tests/engine/graph/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <type_traits>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/graph/recorded_steps.hpp>

namespace
{
    using SushiRuntime::Internal::Graph::RecordedSteps;

    /** @brief Records that it was destroyed and replays nothing. */
    class CountedSteps final : public RecordedSteps
    {
        public:
            /** @brief Makes a step that sets @p destroyed when it goes. */
            explicit CountedSteps(bool& destroyed) noexcept : destroyed_(destroyed)
            {

            }

            /** @brief Sets the flag it was given. */
            ~CountedSteps() override
            {
                destroyed_ = true;
            }

            /** @brief Submits nothing; no test calls it. */
            sycl::event replay(sycl::queue&) override
            {
                return {};
            }

            /** @brief Reads no host words. */
            bool reads_host_words() const noexcept override
            {
                return false;
            }

        private:
            bool& destroyed_; /**< The flag set on destruction. */
    };
} // namespace

/** @brief Tests that a recorded step owned through its base is destroyed whole. */
TEST(Small_RecordedSteps, AStepOwnedThroughItsBaseIsDestroyedWhole)
{
    bool destroyed = false;
    std::unique_ptr<RecordedSteps> step = std::make_unique<CountedSteps>(destroyed);

    step.reset();

    EXPECT_TRUE(destroyed);
}

/** @brief Tests that a recorded step can be neither copied nor moved, so one plan owns it. */
TEST(Small_RecordedSteps, AStepIsNeitherCopiedNorMoved)
{
    EXPECT_FALSE(std::is_copy_constructible_v<CountedSteps>);
    EXPECT_FALSE(std::is_copy_assignable_v<CountedSteps>);
    EXPECT_FALSE(std::is_move_constructible_v<CountedSteps>);
    EXPECT_FALSE(std::is_move_assignable_v<CountedSteps>);
}
