/**************************************************************************/
/* test_emit_device_sized.cpp                                             */
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
 * @file test_emit_device_sized.cpp
 * @brief Tests device_sized_node(): the launch covers the capacity and stops at the live width.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_device_sized.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that only the indices below the device-written live width run. */
TEST(Unit_EmitDeviceSized, OnlyTheLiveIndicesRun)
{
    constexpr std::size_t K_CAPACITY = 8;
    auto runtime = Runtime::create().value();
    int* const data = Testing::shared_integers(runtime, K_CAPACITY, 0);
    std::uint32_t* const live = sycl::malloc_shared<std::uint32_t>(2, Testing::queue_of(runtime));
    ASSERT_NE(data, nullptr);
    ASSERT_NE(live, nullptr);
    live[0] = 3;
    live[1] = 0;
    DeviceSized launch;
    launch.capacity = K_CAPACITY;
    launch.live_width = live;
    launch.live_words = live;
    const Detail::NodeDescription description = Detail::device_sized_node(
        {}, {BufferRegion(data)}, launch, [data](std::size_t index) { data[index] = 1; }, K_CAPACITY);
    EXPECT_EQ(description.width, K_CAPACITY);
    Testing::run_description(runtime, description);
    for (std::size_t index = 0; index < K_CAPACITY; ++index)
        EXPECT_EQ(data[index], index < 3 ? 1 : 0) << "at " << index;
    sycl::free(live, Testing::queue_of(runtime));
    sycl::free(data, Testing::queue_of(runtime));
}
