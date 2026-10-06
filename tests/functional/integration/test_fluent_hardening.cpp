/**************************************************************************/
/* test_fluent_hardening.cpp                                              */
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
 * @file test_fluent_hardening.cpp
 * @brief Tests the multidimensional, variadic, wrapper and captured-argument forms of fluent add().
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/**
 * @brief Tests that add_untracked(Extent<2>, fn) iterates the whole box and writes a row-major
 *        index.
 */
TEST(Integration_FluentHardening, Extent2DPerElementMap)
{
    constexpr std::size_t W = 8, H = 6;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(W * H);
    for (std::size_t i = 0; i < W * H; ++i)
        buf[i] = -1;

    int* p = buf.data();

    auto g = rt.graph();
    g.add_untracked(Extent{W, H}, [p](sycl::id<2> idx)
    {
        const std::size_t x = idx[0];
        const std::size_t y = idx[1];
        p[y * W + x] = static_cast<int>(y * W + x);
    });
    g.run();

    for (std::size_t i = 0; i < W * H; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
}

/**
 * @brief Tests that add_untracked(NdRange<2>, fn) runs a tiled kernel that writes its global linear
 *        index.
 */
TEST(Integration_FluentHardening, NdRange2DTiledKernel)
{
    constexpr std::size_t W = 8, H = 8;   // tiles by 4x4

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(W * H);
    for (std::size_t i = 0; i < W * H; ++i)
        buf[i] = -1;

    int* p = buf.data();

    auto g = rt.graph();
    g.add_untracked(NdRange{ sycl::range<2>(W, H), sycl::range<2>(4, 4) },
                    [p](sycl::nd_item<2> it)
    {
        const std::size_t x = it.get_global_id(0);
        const std::size_t y = it.get_global_id(1);
        p[y * W + x] = static_cast<int>(y * W + x);
    });
    g.run();

    for (std::size_t i = 0; i < W * H; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
}

/** @brief Tests that add(State, Extent<2>, fn) leaves S*y in row y after S steps. */
TEST(Integration_FluentHardening, State2DSteppedUpdate)
{
    constexpr std::size_t W = 4, H = 4, S = 3;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(W * H);
    for (std::size_t i = 0; i < W * H; ++i)
        field[i] = 0;

    auto g = rt.graph();
    g.add(field, Extent{W, H}, [](sycl::id<2> idx, const int* cur, int* next)
    {
        const std::size_t x = idx[0];
        const std::size_t y = idx[1];
        next[y * W + x] = cur[y * W + x] + static_cast<int>(y);
    });
    g.run(S);

    for (std::size_t y = 0; y < H; ++y)
        for (std::size_t x = 0; x < W; ++x)
            ASSERT_EQ(field[y * W + x], static_cast<int>(S * y))
                << "at (" << x << "," << y << ")";
}

/** @brief Tests that add(out, in0, in1, fn) computes next = cur + a + b from previous-step data. */
TEST(Integration_FluentHardening, VariadicCoupledThreeInputs)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create();
    auto u  = rt.state<int>(N);
    auto a  = rt.state<int>(N);
    auto b  = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        u[i] = 0;
        a[i] = 2;
        b[i] = 3;
    }

    auto g = rt.graph();
    // a and b have their own (identity) update rules so they advance each step.
    g.add(a, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i]; });
    g.add(b, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i]; });
    g.add(u, a, b, [](std::size_t i, const int* cur, int* next,
                      const int* av, const int* bv)
    {
        next[i] = cur[i] + av[i] + bv[i];
    });
    g.run(4);

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(u[i], 4 * (2 + 3)) << "at i=" << i;
}

/** @brief Tests that the Reads and Writes wrappers order a writer before a dependent reader. */
TEST(Integration_FluentHardening, ReadsWritesWrappersOrdering)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add(Reads{}, Writes{ buf }, N, [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(Reads{ buf }, Writes{ buf }, N, [p](std::size_t i) { p[i] = p[i] * p[i]; });
    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
}

/**
 * @brief Tests that In and Out captured arguments hand the kernel raw pointers and register the
 *        dependency.
 */
TEST(Integration_FluentHardening, InOutCapturedArguments)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto src = rt.buffer<int>(N);
    auto dst = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        src[i] = static_cast<int>(i);
        dst[i] = 0;
    }

    auto g = rt.graph();
    g.add(Extent{N}, In(src), Out(dst),
          [](sycl::id<1> i, const int* s, int* d) { d[i[0]] = s[i[0]] * 2; });
    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(dst[i], static_cast<int>(i) * 2) << "at i=" << i;
}

/** @brief Tests that an interior-only update leaves the one-cell border untouched. */
TEST(Integration_FluentHardening, InteriorOnlyHaloUpdate)
{
    constexpr std::size_t W = 6, H = 6;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(W * H);
    for (std::size_t i = 0; i < W * H; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    // Iterate the (W-2)x(H-2) interior box starting at (1,1); the border is skipped.
    Extent<2> interior{ sycl::range<2>(W - 2, H - 2), sycl::id<2>(1, 1) };
    g.add(Reads{ buf }, Writes{ buf }, interior, [p](sycl::id<2> idx)
    {
        p[idx[1] * W + idx[0]] += 1;
    });
    g.run();

    for (std::size_t y = 0; y < H; ++y)
        for (std::size_t x = 0; x < W; ++x)
        {
            const bool border = (x == 0 || y == 0 || x == W - 1 || y == H - 1);
            const int expected = border ? 0 : 1;
            ASSERT_EQ(buf[y * W + x], expected) << "at (" << x << "," << y << ")";
        }
}
