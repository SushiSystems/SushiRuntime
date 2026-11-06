/**************************************************************************/
/* test_errors.cpp                                                        */
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
 * @file test_errors.cpp
 * @brief Tests that a failing node is reported and leaves the Runtime usable.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the error tests, which run on the program's runtime. */
class Medium_ErrorIsolation : public TestSupport::RuntimeTest
{
};

/** @brief Tests that a throwing node is named, with its registration index, in the error run() returns. */
TEST_F(Medium_ErrorIsolation, ThrowingNodeIsReportedByNameAndRegistrationIndex)
{
    auto g  = runtime().graph();
    g.add_host([] {}).name("healthy");
    g.add_host([] { throw std::runtime_error("boom"); }).name("faulty");

    const auto result = g.run();

    ASSERT_FALSE(result);
    const std::string& message = result.error().message;
    EXPECT_NE(message.find("node 'faulty' (registration index 1)"), std::string::npos) << message;
    EXPECT_NE(message.find("boom"), std::string::npos) << message;
    EXPECT_EQ(message.find("healthy"), std::string::npos) << message;
}

/** @brief Tests that a graph runs clean on the same runtime after a graph whose kernel threw failed its run. */
TEST_F(Medium_ErrorIsolation, AGraphRunsCleanAfterAKernelThrew)
{
    constexpr std::size_t N = 64;
    auto bad = runtime().graph();
    bad.add_untracked([](sycl::handler&) { throw std::runtime_error("boom"); });
    ASSERT_FALSE(bad.run().has_value());
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto good = runtime().graph();
    good.add_untracked(N, [p](std::size_t i) { p[i] = 42; });

    good.run().value();

    EXPECT_EQ(TestSupport::contents(buf), std::vector<int>(N, 42));
}
