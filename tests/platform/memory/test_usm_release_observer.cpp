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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_usm_release_observer.cpp
 * @brief Tests USMReleaseObserver: the allocator tells an observer of each release, with its address.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

using namespace SushiRuntime::Internal::Memory;

namespace
{
    /** @brief Records the address of each release it is told of. */
    class RecordingObserver final : public USMReleaseObserver
    {
        public:
            void on_release(void* address) noexcept override
            {
                released.push_back(address);
            }

            /** @brief Holds the addresses in the order they were released. */
            std::vector<void*> released;
    };
}

/** @brief Tests that the observer hears the release of the address the allocator gave. */
TEST(Unit_USMReleaseObserver, HearsEachReleaseWithItsAddress)
{
    if (sycl::device::get_devices().empty())
        GTEST_SKIP() << "no SYCL device";
    USMAllocator allocator({sycl::queue(sycl::device::get_devices().front())});
    RecordingObserver observer;
    allocator.set_release_observer(&observer);

    USMRequest request;
    request.kind = USMKind::Shared;
    request.bytes = 64;
    void* const address = allocator.allocate(request);
    if (address == nullptr)
        GTEST_SKIP() << "the device gives no shared memory";
    allocator.release(address);

    ASSERT_EQ(observer.released.size(), 1u);
    EXPECT_EQ(observer.released.front(), address);
    allocator.set_release_observer(nullptr);
}
