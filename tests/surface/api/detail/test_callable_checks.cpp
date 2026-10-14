/**************************************************************************/
/* test_callable_checks.cpp                                               */
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
 * @file test_callable_checks.cpp
 * @brief Tests the kernel traits: which callable each launch form takes.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/callable_checks.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests the index type each launch form passes its kernel. */
TEST(Unit_CallableChecks, EachLaunchTakesItsIndex)
{
    const auto by_count = [](std::size_t) {};
    const auto by_id = [](sycl::id<2>) {};
    const auto by_item = [](sycl::nd_item<1>) {};
    static_assert(Detail::is_launch_kernel_v<std::size_t, decltype(by_count)>, "a count takes size_t");
    static_assert(!Detail::is_launch_kernel_v<std::size_t, decltype(by_id)>, "a count does not take an id");
    static_assert(Detail::is_launch_kernel_v<Extent<2>, decltype(by_id)>, "an extent takes an id");
    static_assert(Detail::is_launch_kernel_v<NdRange<1>, decltype(by_item)>, "a range takes an nd_item");
    static_assert(Detail::accepts_kernel<std::size_t, decltype(by_count)>(), "accepted");
    SUCCEED();
}
