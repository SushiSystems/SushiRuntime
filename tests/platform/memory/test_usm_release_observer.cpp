/**************************************************************************/
/* test_usm_release_observer.cpp                                          */
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
 * @file test_usm_release_observer.cpp
 * @brief Tests USMReleaseObserver: the allocator tells an observer of each release, with its address.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/recording_usm_release_observer.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

using namespace SushiRuntime::Internal::Memory;

/** @brief Tests that the observer hears the release of the address the allocator gave. */
TEST(Small_USMReleaseObserver, HearsEachReleaseWithItsAddress)
{
    USMAllocator allocator(std::make_unique<HostUSMSource>());
    RecordingUSMReleaseObserver observer;
    allocator.set_release_observer(&observer);

    USMRequest request;
    request.kind = USMKind::Shared;
    request.bytes = 64;
    void* const address = allocator.allocate(request);
    ASSERT_NE(address, nullptr);
    allocator.release(address);

    EXPECT_EQ(observer.released(), std::vector<void*>{address});
    allocator.set_release_observer(nullptr);
}
