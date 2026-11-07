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

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

static_assert(In<int>::reads && !In<int>::writes, "In reads only");
static_assert(Out<int>::writes && !Out<int>::reads, "Out writes only");
static_assert(InOut<float>::reads && InOut<float>::writes, "InOut reads and writes");

/** @brief Fixture of the capture tests, which allocate on a host runtime. */
class Small_Captures : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that In and Out point at the buffer they name and In observes it. */
TEST_F(Small_Captures, EachCaptureNamesItsBuffer)
{
    auto in = runtime().buffer<int>(4).value();
    auto out = runtime().buffer<int>(4).value();

    const In<int> read(in);
    const Out<int> written(out);

    EXPECT_EQ(read.ptr, in.data());
    EXPECT_EQ(read.key, static_cast<void*>(in.data()));
    EXPECT_EQ(written.ptr, out.data());
    EXPECT_FALSE(read.life.expired());
}
