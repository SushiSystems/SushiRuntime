/**************************************************************************/
/* test_captures.cpp                                                      */
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
 * @file test_captures.cpp
 * @brief Tests In, Out and InOut: the pointer, the key and the observer each capture holds.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/vocabulary/captures.hpp>

using namespace SushiRuntime;

/** @brief Tests that In reads, Out writes, and both point at the buffer they name. */
TEST(Medium_Captures, EachCaptureNamesItsBuffer)
{
    auto runtime = Runtime::create().value();
    auto in = runtime.buffer<int>(4).value();
    auto out = runtime.buffer<int>(4).value();
    const In<int> read(in);
    const Out<int> written(out);
    static_assert(In<int>::reads && !In<int>::writes, "In reads only");
    static_assert(Out<int>::writes && !Out<int>::reads, "Out writes only");
    EXPECT_EQ(read.ptr, in.data());
    EXPECT_EQ(read.key, static_cast<void*>(in.data()));
    EXPECT_EQ(written.ptr, out.data());
    EXPECT_FALSE(read.life.expired());
}

/** @brief Tests that InOut both reads and writes. */
TEST(Medium_Captures, InOutReadsAndWrites)
{
    static_assert(InOut<float>::reads && InOut<float>::writes, "InOut reads and writes");
    SUCCEED();
}
