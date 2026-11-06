/**************************************************************************/
/* test_recording_allocation_owner.cpp                                    */
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
 * @file test_recording_allocation_owner.cpp
 * @brief Tests RecordingAllocationOwner: releases are recorded in order, one per Allocation.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/recording_allocation_owner.hpp>

using SushiRuntime::Detail::Allocation;
using SushiRuntime::Detail::RecordingAllocationOwner;

namespace
{
    /** @brief Returns a distinct non-null address that is never dereferenced. */
    void* fake(std::uintptr_t n)
    {
        return reinterpret_cast<void*>(n * 4096u);
    }
} // namespace

/** @brief Tests that the owner keeps each released address in call order, repeats included. */
TEST(Small_RecordingAllocationOwner, KeepsEachReleaseInCallOrder)
{
    std::vector<void*> releases;
    RecordingAllocationOwner owner(releases);

    owner.release(fake(1));
    owner.release(fake(2));
    owner.release(fake(1));

    const std::vector<void*> expected{fake(1), fake(2), fake(1)};
    EXPECT_EQ(releases, expected);
}

/** @brief Tests that an Allocation releases its address once, through the owner. */
TEST(Small_RecordingAllocationOwner, AnAllocationReleasesItsAddressOnceThroughIt)
{
    std::vector<void*> releases;
    {
        Allocation allocation(fake(7), 64, std::make_unique<RecordingAllocationOwner>(releases));
        EXPECT_TRUE(releases.empty()) << "nothing is released while the allocation is alive";
    }
    ASSERT_EQ(releases.size(), 1u);
    EXPECT_EQ(releases.front(), fake(7));
}
