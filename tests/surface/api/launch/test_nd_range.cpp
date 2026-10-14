/**************************************************************************/
/* test_nd_range.cpp                                                      */
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
 * @file test_nd_range.cpp
 * @brief Tests NdRange: the global count, the SYCL range it builds and at().
 * @see tests/surface/api/launch/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>

using namespace SushiRuntime;

/** @brief Tests that the count is the global size and the SYCL range keeps both sizes. */
TEST(Unit_NdRange, CountIsTheGlobalSize)
{
    const NdRange<2> range(sycl::range<2>(8, 4), sycl::range<2>(4, 2));
    EXPECT_EQ(range.count(), 32u);
    const sycl::nd_range<2> nd = range.to_nd_range();
    EXPECT_EQ(nd.get_global_range(), (sycl::range<2>(8, 4)));
    EXPECT_EQ(nd.get_local_range(), (sycl::range<2>(4, 2)));
}

/** @brief Tests that at() keeps both sizes and moves the origin. */
TEST(Unit_NdRange, AtMovesTheOrigin)
{
    const NdRange<1> range(sycl::range<1>(16), sycl::range<1>(4));
    const NdRange<1> moved = range.at(sycl::id<1>(8));
    EXPECT_EQ(moved.global, range.global);
    EXPECT_EQ(moved.local, range.local);
    EXPECT_EQ(moved.origin, sycl::id<1>(8));
}
