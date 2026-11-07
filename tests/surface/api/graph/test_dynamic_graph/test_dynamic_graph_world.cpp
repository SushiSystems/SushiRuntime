/**************************************************************************/
/* test_dynamic_graph_world.cpp                                           */
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
 * @file test_dynamic_graph_world.cpp
 * @brief Tests that a world of 32 regions composes once and recomposes once per mutation.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Tests that a world of 32 regions composes once and recomposes once per mutation. */
namespace DynamicGraphWorldTests
{
    /**
     * @brief Fixture of a world of 32 regions that each add one to their own buffer and have run three steps.
     */
    class Small_DynamicGraphWorld : public TestSupport::HostRuntimeTest
    {
        protected:
            static constexpr std::size_t N       = 4;
            static constexpr std::size_t K_REGIONS = 32;

            /** @brief Records the 32 regions and runs three steps. */
            void SetUp() override
            {
                TestSupport::HostRuntimeTest::SetUp();
                bufs_.reserve(K_REGIONS);
                for (std::size_t r = 0; r < K_REGIONS; ++r)
                    bufs_.push_back(runtime().buffer<int>(N).value());
                for (auto& buf : bufs_)
                    for (std::size_t i = 0; i < N; ++i)
                        buf[i] = 0;

                world_.emplace(runtime().dynamic_graph());
                for (std::size_t r = 0; r < K_REGIONS; ++r)
                {
                    int* p = bufs_[r].data();
                    world_->region(r).add(N, Reads{}, Writes(bufs_[r]),
                                          [p](std::size_t i) { p[i] += 1; });
                }

                world_->run(3).value();
            }

            /** @brief Adds ten to every cell of region 7 and runs one step. */
            void mutate_region_seven_and_run()
            {
                int* p7 = bufs_[7].data();
                world_->region(7).add(N, Reads{}, Writes(bufs_[7]),
                                      [p7](std::size_t i) { p7[i] += 10; });
                world_->run(1).value();
            }

            std::vector<Buffer<int>> bufs_;    /**< One buffer per region. */
            std::optional<DynamicGraph> world_; /**< The graph of the 32 regions. */
    };

    /** @brief Tests that building all regions composes once. */
    TEST_F(Small_DynamicGraphWorld, BuildingAllRegionsComposesOnce)
    {
        EXPECT_EQ(world_->compose_count(), 1u) << "building all regions once composes once.";
    }

    /** @brief Tests that one mutation recomposes exactly once, not once per region. */
    TEST_F(Small_DynamicGraphWorld, OneMutationRecomposesExactlyOnce)
    {
        mutate_region_seven_and_run();

        EXPECT_EQ(world_->compose_count(), 2u) << "one mutation recomposes exactly once.";
    }

    /** @brief Tests that after one mutation every region holds the count its runs give it. */
    TEST_F(Small_DynamicGraphWorld, EveryRegionHoldsItsRunCountAfterAMutation)
    {
        mutate_region_seven_and_run();

        for (std::size_t r = 0; r < K_REGIONS; ++r)
            for (std::size_t i = 0; i < N; ++i)
            {
                const int expected = (r == 7) ? (4 + 10) : 4;  // 3 steps + 1 step (+10 on its 4th)
                ASSERT_EQ(bufs_[r][i], expected) << "region " << r << " at i=" << i;
            }
    }
} // namespace DynamicGraphWorldTests
