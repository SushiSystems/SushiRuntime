/**************************************************************************/
/* test_extent.cpp                                                        */
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
 * @file test_extent.cpp
 * @brief Tests Extent: the count of each rank, the origin and at().
 * @see tests/surface/api/launch/README.md
 * @author Mustafa Garip
 */

#include <type_traits>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/launch/extent.hpp>

using namespace SushiRuntime;

/** @brief Tests the count of a one-, a two- and a three-dimensional extent. */
TEST(Small_Extent, CountIsTheProductOfTheSizes)
{
    EXPECT_EQ(Extent<1>(8).count(), 8u);
    EXPECT_EQ(Extent(4, 3).count(), 12u);
    EXPECT_EQ(Extent(2, 3, 4).count(), 24u);
    static_assert(std::is_same_v<decltype(Extent(4, 3)), Extent<2>>, "two sizes deduce rank 2");
}

/** @brief Tests that at() keeps the sizes and moves the origin. */
TEST(Small_Extent, AtMovesTheOrigin)
{
    const Extent<2> extent(4, 3);
    EXPECT_EQ(extent.origin, (sycl::id<2>{0, 0}));
    const Extent<2> moved = extent.at(sycl::id<2>{1, 2});
    EXPECT_EQ(moved.extent, extent.extent);
    EXPECT_EQ(moved.origin, (sycl::id<2>{1, 2}));
}
