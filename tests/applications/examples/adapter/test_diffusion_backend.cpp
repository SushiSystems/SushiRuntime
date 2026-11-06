/**************************************************************************/
/* test_diffusion_backend.cpp                                             */
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
 * @file test_diffusion_backend.cpp
 * @brief Tests the adapter example's backend: the host step, a device run against it, and the status names.
 * @see tests/applications/examples/adapter/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "adapter/diffusion_backend.hpp"
#include "support/runtime_environment.hpp"
#include "support/tolerance.hpp"

using namespace HostProgram;

/** @brief Tests that one host step leaves a uniform field unchanged, edges included. */
TEST(Small_DiffusionBackend, AUniformFieldStaysUniform)
{
    std::vector<float> field(8, 3.0f);

    diffuse_on_host(field, 0.25f);

    EXPECT_EQ(field, std::vector<float>(8, 3.0f));
}

/** @brief Tests that one host step moves a lone peak by rate toward each neighbour. */
TEST(Small_DiffusionBackend, APeakSpreadsByTheRate)
{
    std::vector<float> field{0.0f, 1.0f, 0.0f};

    diffuse_on_host(field, 0.25f);

    EXPECT_EQ(field, (std::vector<float>{0.25f, 0.5f, 0.25f}));
}

/** @brief Runs the backend on the program's one runtime. */
class Medium_DiffusionBackend : public TestSupport::RuntimeTest
{
};

/** @brief Tests that ten device steps on the program's runtime match ten host steps. */
TEST_F(Medium_DiffusionBackend, TenDeviceStepsMatchTheHostSteps)
{
    BackendHandle backend;
    const Status created = create_backend(backend, runtime());
    ASSERT_TRUE(created.is_ok()) << created.message;
    std::vector<float> expected(16, 0.0f);
    expected[8] = 1.0f;
    const Status allocated = allocate_field(*backend, expected, 0.25f);
    ASSERT_TRUE(allocated.is_ok()) << allocated.message;

    const Status advanced = advance(*backend, 10);
    std::vector<float> values;
    const Status read = read_field(*backend, values);

    ASSERT_TRUE(advanced.is_ok()) << advanced.message;
    ASSERT_TRUE(read.is_ok()) << read.message;
    for (int step = 0; step < 10; ++step)
        diffuse_on_host(expected, 0.25f);
    ASSERT_EQ(values.size(), expected.size());
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        // Each step adds at most 4 roundings per cell to a value at most 1.
        const TestSupport::ErrorRatio ratio = TestSupport::measure_error(
            values[index], expected[index], 10 * 4 * TestSupport::unit_roundoff<float>());
        EXPECT_TRUE(ratio.is_within()) << "at " << index << ": " << ratio.describe();
    }
}
/** @brief Tests that every status code has its own name, spelled as the code is. */
TEST(Small_DiffusionBackend, EveryCodeHasAName)
{
    EXPECT_STREQ(to_string(StatusCode::Ok), "Ok");
    EXPECT_STREQ(to_string(StatusCode::OutOfMemory), "OutOfMemory");
    EXPECT_STREQ(to_string(StatusCode::DeviceFailed), "DeviceFailed");
    EXPECT_STREQ(to_string(StatusCode::InvalidRequest), "InvalidRequest");
    EXPECT_STREQ(to_string(StatusCode::Unsupported), "Unsupported");
    EXPECT_STREQ(to_string(StatusCode::BackendFault), "BackendFault");
}
