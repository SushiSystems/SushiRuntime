/**************************************************************************/
/* test_callable_traits.cpp                                               */
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
 * @file test_callable_traits.cpp
 * @brief Tests that the callable traits judge a kernel as the const object an emitter calls.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/callable_checks.hpp>
#include <SushiRuntime/api/detail/callable_traits.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of the States the coupled kernels below take. */
    using Word = std::uint32_t;
} // namespace

/** @brief Tests that a per-element kernel passes without mutable and fails with it. */
TEST(Integration_CallableTraits, AMutablePerElementKernelIsRejected)
{
    auto plain = [](std::size_t) {};

    // A lambda without captures converts to a function pointer and stays callable as const.
    int calls = 0;
    auto mutating = [calls](std::size_t) mutable { ++calls; };

    EXPECT_TRUE(Detail::is_per_element_v<decltype(plain)>);
    EXPECT_FALSE(Detail::is_per_element_v<decltype(mutating)>);
    EXPECT_FALSE(Detail::is_per_element_v<decltype(mutating)&>);
}

/** @brief Tests that a range kernel passes without mutable and fails with it. */
TEST(Integration_CallableTraits, AMutableRangeKernelIsRejected)
{
    auto ranging = [](std::size_t, std::size_t, sycl::handler&) {};
    int calls = 0;
    auto mutating = [calls](std::size_t, std::size_t, sycl::handler&) mutable { ++calls; };

    EXPECT_TRUE(Detail::is_range_kernel_v<decltype(ranging)>);
    EXPECT_FALSE(Detail::is_per_element_v<decltype(ranging)>);
    EXPECT_FALSE(Detail::is_range_kernel_v<decltype(mutating)>);
}

/** @brief Tests that a mutable raw kernel still passes, since raw_node() calls it non-const. */
TEST(Integration_CallableTraits, AMutableRawKernelIsAccepted)
{
    auto mutating = [](sycl::handler&) mutable {};

    EXPECT_TRUE(Detail::is_raw_kernel_v<decltype(mutating)>);
}

/** @brief Tests that a coupled kernel fits only when it takes each State read as const. */
TEST(Integration_CallableTraits, ACoupledKernelMustTakeItsInputsAsConst)
{
    auto reading = [](std::size_t, const Word*, Word*, const Word*) {};
    auto writing = [](std::size_t, const Word*, Word*, Word*) {};
    auto boxed = [](sycl::id<1>, const Word*, Word*, const Word*) {};
    auto boxed_writing = [](sycl::id<1>, const Word*, Word*, Word*) {};

    EXPECT_TRUE((Detail::is_launch_kernel_v<std::size_t, decltype(reading), const Word*,
                                                 Word*, const Word*>));
    EXPECT_FALSE((Detail::is_launch_kernel_v<std::size_t, decltype(writing), const Word*,
                                                  Word*, const Word*>));
    EXPECT_TRUE((Detail::is_launch_kernel_v<Extent<1>, decltype(boxed), const Word*,
                                                 Word*, const Word*>));
    EXPECT_FALSE((Detail::is_launch_kernel_v<Extent<1>, decltype(boxed_writing),
                                                  const Word*, Word*, const Word*>));
}
