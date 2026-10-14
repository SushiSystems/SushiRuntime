/**************************************************************************/
/* test_usm_shared.cpp                                                    */
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
 * @file test_usm_shared.cpp
 * @brief Tests USMShared: an allocation the host reads and writes, freed once, moved without a copy.
 * @see tests/foundation/usm_guard/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <utility>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/usm_guard/usm_shared.hpp>

using namespace SushiRuntime::Test;

/** @brief Tests that the host writes and reads every element. */
TEST(Unit_USMShared, TheHostWritesAndReadsEveryElement)
{
    if (!has_any_sycl_device())
        GTEST_SKIP() << "no SYCL device";
    sycl::queue queue;
    USMShared<int> values(queue, 16);
    ASSERT_TRUE(values);
    EXPECT_EQ(values.count(), 16u);
    for (std::size_t index = 0; index < values.count(); ++index)
        values[index] = static_cast<int>(index) * 3;
    for (std::size_t index = 0; index < values.count(); ++index)
        EXPECT_EQ(values.get()[index], static_cast<int>(index) * 3);
}

/** @brief Tests that a move hands the allocation over and empties the source. */
TEST(Unit_USMShared, AMoveHandsTheAllocationOver)
{
    if (!has_any_sycl_device())
        GTEST_SKIP() << "no SYCL device";
    sycl::queue queue;
    USMShared<float> source(queue, 4);
    float* const address = source.get();
    USMShared<float> target(std::move(source));
    EXPECT_FALSE(source);
    EXPECT_EQ(target.get(), address);

    USMShared<float> other(queue, 2);
    other = std::move(target);
    EXPECT_EQ(other.get(), address);
    EXPECT_EQ(other.count(), 4u);
}
