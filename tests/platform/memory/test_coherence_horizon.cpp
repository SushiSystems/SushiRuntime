/**************************************************************************/
/* test_coherence_horizon.cpp                                             */
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
 * @file test_coherence_horizon.cpp
 * @brief Tests that CoherenceHorizon retires a directory's coherence history every N steps.
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/coherence_horizon.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    constexpr std::uint32_t K_UNIVERSE = 7;
    const MemorySpaceID K_HOST_SPACE(0);
    const MemorySpaceID K_DEVICE_SPACE(1);

    /** @brief Returns a directory entry whose map holds two written bytes at two versions: three intervals. */
    GlobalHandle mint_with_history(ResidencyDirectory& directory)
    {
        const MintOutcome outcome = directory.mint(HandleKind::Buffer, HandleFlags::None);
        EXPECT_EQ(outcome.status, HandleStatus::Ok);
        CoherenceIntervals& map = *directory.coherence(outcome.handle);
        map.reset(8, K_HOST_SPACE);
        map.record_write(0, 1, K_DEVICE_SPACE);
        map.record_write(1, 2, K_DEVICE_SPACE);
        return outcome.handle;
    }

    TEST(Small_CoherenceHorizon, NoHorizonFallsBeforeTheIntervalIsReached)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle handle = mint_with_history(directory);
        CoherenceHorizon horizon(4);

        EXPECT_FALSE(horizon.advance(directory, 3));
        EXPECT_EQ(directory.coherence(handle)->intervals().size(), 3U);
    }

    TEST(Small_CoherenceHorizon, TheStepThatReachesTheIntervalRetiresTheHistory)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        const GlobalHandle handle = mint_with_history(directory);
        CoherenceHorizon horizon(4);
        ASSERT_FALSE(horizon.advance(directory, 3));

        EXPECT_TRUE(horizon.advance(directory, 1));
        // Bytes 0 and 1 both hold only the device once their versions are dropped.
        EXPECT_EQ(directory.coherence(handle)->intervals().size(), 2U);
    }

    TEST(Small_CoherenceHorizon, CountingStartsAgainAfterAHorizon)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        CoherenceHorizon horizon(4);
        ASSERT_TRUE(horizon.advance(directory, 5));

        EXPECT_FALSE(horizon.advance(directory, 3));
        EXPECT_TRUE(horizon.advance(directory, 1));
    }

    TEST(Small_CoherenceHorizon, AnIntervalOfZeroFallsEveryStep)
    {
        ResidencyDirectory directory(K_UNIVERSE);
        CoherenceHorizon horizon(0);

        EXPECT_EQ(horizon.steps_per_horizon(), 1U);
        EXPECT_TRUE(horizon.advance(directory, 1));
    }
} // namespace
