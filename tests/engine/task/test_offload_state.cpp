/**************************************************************************/
/* test_offload_state.cpp                                                 */
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
 * @file test_offload_state.cpp
 * @brief Tests OffloadState: not offloadable, no attempt, no lists and no payload until set.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/task/offload_state.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Counts its own destruction. */
    class CountingPayload final : public OffloadPayload
    {
        public:
            explicit CountingPayload(int& destroyed) : destroyed_(destroyed)
            {

            }

            ~CountingPayload() override
            {
                ++destroyed_;
            }

        private:
            int& destroyed_;
    };
}

/** @brief Tests the defaults, and that the state owns its payload. */
TEST(Small_OffloadState, DefaultsHoldNothingAndThePayloadIsOwned)
{
    int destroyed = 0;
    {
        OffloadState state;
        EXPECT_FALSE(state.offloadable);
        EXPECT_EQ(state.attempts.load(), 0u);
        EXPECT_EQ(state.live_reads, nullptr);
        EXPECT_EQ(state.payload, nullptr);
        state.payload = std::make_unique<CountingPayload>(destroyed);
    }
    EXPECT_EQ(destroyed, 1);
}
