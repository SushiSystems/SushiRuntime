/**************************************************************************/
/* test_advanced_controls.cpp                                             */
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
 * @file test_advanced_controls.cpp
 * @brief Tests the rebalancer control.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the rebalancer tests on the program's runtime, which gives the rebalancer back off. */
class Medium_PerfControls : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Turns the rebalancer off, so the next test finds the default. */
        void TearDown() override { runtime().set_rebalancer_enabled(false); }
};

/** @brief Tests that a runtime built with no config does not run the rebalancer. */
TEST_F(Medium_PerfControls, RebalancerIsOffByDefault)
{
    EXPECT_FALSE(runtime().advanced().is_rebalancer_enabled())
        << "the rebalancer must not run unless RuntimeConfig asked for it";
}

/** @brief Tests that RuntimeConfig::rebalancer starts the rebalancer. */
TEST_F(Medium_PerfControls, RebalancerIsOnWhenRequested)
{
    RuntimeConfig config;
    config.rebalancer = true;

    auto throughput = Runtime::create(config).value();

    EXPECT_TRUE(throughput.advanced().is_rebalancer_enabled())
        << "RuntimeConfig::rebalancer must start the coordinator";
}

/** @brief Fixture of the run tests; the parameter is whether the rebalancer is on during the run. */
class Medium_PerfControlsRun : public Medium_PerfControls,
                               public ::testing::WithParamInterface<bool>
{
};

/** @brief Tests that a run gives the same result with the rebalancer off and on. */
TEST_P(Medium_PerfControlsRun, RebalancerToggleKeepsExecutionCorrect)
{
    constexpr std::size_t N = 128;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
    runtime().set_rebalancer_enabled(GetParam());

    g.run().value();

    EXPECT_EQ(runtime().advanced().is_rebalancer_enabled(), GetParam());
    EXPECT_EQ(TestSupport::contents(buf), TestSupport::ramp<int>(N, 1));
}

INSTANTIATE_TEST_SUITE_P(Rebalancer, Medium_PerfControlsRun, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info)
                         {
                             return info.param ? "On" : "Off";
                         });
