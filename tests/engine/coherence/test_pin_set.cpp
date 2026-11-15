/**************************************************************************/
/* test_pin_set.cpp                                                       */
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
 * @file test_pin_set.cpp
 * @brief Tests which pairs PinSet reports as pinned.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/pin_set.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    const GlobalHandle K_BUFFER(1, 0, HandleKind::Buffer, HandleFlags::None, 1, 7);
    const GlobalHandle K_OTHER(1, 0, HandleKind::Buffer, HandleFlags::None, 1, 8);
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);

    TEST(Small_PinSet, APinHoldsForItsBufferAndSpaceOnly)
    {
        PinSet subject;
        subject.pin(K_BUFFER, K_DEVICE_0);
        EXPECT_TRUE(subject.is_pinned(K_BUFFER, K_DEVICE_0));
        EXPECT_FALSE(subject.is_pinned(K_BUFFER, K_DEVICE_1));
        EXPECT_FALSE(subject.is_pinned(K_OTHER, K_DEVICE_0));
    }

    TEST(Small_PinSet, PinningTwiceCountsOnceAndUnpinningRemovesIt)
    {
        PinSet subject;
        subject.pin(K_BUFFER, K_DEVICE_0);
        subject.pin(K_BUFFER, K_DEVICE_0);
        EXPECT_EQ(subject.size(), 1U);

        subject.unpin(K_OTHER, K_DEVICE_0);
        EXPECT_EQ(subject.size(), 1U);
        subject.unpin(K_BUFFER, K_DEVICE_0);
        EXPECT_EQ(subject.size(), 0U);
        EXPECT_FALSE(subject.is_pinned(K_BUFFER, K_DEVICE_0));
    }
}
