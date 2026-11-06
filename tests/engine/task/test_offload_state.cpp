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
 * @brief Tests OffloadState: the state owns its payload.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/task/offload_state.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Counts its own destruction; no payload type ships in the task module to watch instead. */
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

/** @brief Tests that the state owns its payload and destroys it with itself. */
TEST(Small_OffloadState, ThePayloadIsOwned)
{
    int destroyed = 0;
    {
        OffloadState state;
        state.payload = std::make_unique<CountingPayload>(destroyed);
        EXPECT_EQ(destroyed, 0) << "the payload was destroyed while the state held it";
    }
    EXPECT_EQ(destroyed, 1);
}
