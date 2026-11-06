/**************************************************************************/
/* test_recording_usm_release_observer.cpp                                */
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
 * @file test_recording_usm_release_observer.cpp
 * @brief Tests RecordingUSMReleaseObserver: each address heard is kept, in arrival order.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/recording_usm_release_observer.hpp>

using SushiRuntime::Internal::Memory::RecordingUSMReleaseObserver;

namespace
{
    /** @brief Returns a distinct non-null address that is never dereferenced. */
    void* fake(std::uintptr_t n)
    {
        return reinterpret_cast<void*>(n * 4096u);
    }
} // namespace

/** @brief Tests that the observer keeps each address it hears, in arrival order. */
TEST(Small_RecordingUSMReleaseObserver, KeepsEachAddressInArrivalOrder)
{
    RecordingUSMReleaseObserver observer;

    observer.on_release(fake(1));
    observer.on_release(fake(2));
    observer.on_release(fake(1));

    const std::vector<void*> expected{fake(1), fake(2), fake(1)};
    EXPECT_EQ(observer.released(), expected);
}
