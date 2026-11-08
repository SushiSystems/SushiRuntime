/**************************************************************************/
/* test_dispatch_placement.cpp                                            */
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
 * @file test_dispatch_placement.cpp
 * @brief Tests place_for_dispatch(): the device a node is placed on at its first dispatch, and the refusals.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/scheduler/dispatch_placement.hpp>

using SushiRuntime::make_sushi;
using SushiRuntime::sushi_ptr;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Scheduler::place_for_dispatch;

/** @brief Holds host memory of two devices and the node under test. */
class Small_DispatchPlacement : public ::testing::Test
{
    protected:
        /** @brief The device count the placement is checked against. */
        static constexpr std::size_t K_DEVICES = 2;

        /// @brief Host memory of two devices; a test records ownership with adopt().
        SushiRuntime::Internal::Memory::USMAllocator memory_{
            SushiRuntime::Internal::Memory::make_host_usm_source(K_DEVICES)};
        /// @brief The node placed.
        sushi_ptr<Node> node_ = make_sushi<Node>();
};

/** @brief Tests that a node with no region and nothing stated is placed on device 0 and marked placed. */
TEST_F(Small_DispatchPlacement, CachesTheDefaultDeviceOfARegionlessNode)
{
    ASSERT_TRUE(place_for_dispatch(*node_, memory_, K_DEVICES));

    EXPECT_EQ(node_->cached_queue_index_, 0);
    EXPECT_TRUE(node_->locality_checked_);
}

/** @brief Tests that the device the placement rule picks, the owner of the node's region, is cached. */
TEST_F(Small_DispatchPlacement, CachesTheOwnerOfTheNodesRegion)
{
    int cell = 0;
    memory_.adopt(&cell, 1, sizeof(cell), std::nullopt);
    node_->io_writes_.emplace_back(&cell);

    ASSERT_TRUE(place_for_dispatch(*node_, memory_, K_DEVICES));

    EXPECT_EQ(node_->cached_queue_index_, 1);
    memory_.forget(&cell);
}

/** @brief Tests that a placed node keeps its device when its region's owner changes before a later dispatch. */
TEST_F(Small_DispatchPlacement, APlacedNodeKeepsItsDeviceOnALaterDispatch)
{
    int cell = 0;
    memory_.adopt(&cell, 1, sizeof(cell), std::nullopt);
    node_->io_writes_.emplace_back(&cell);
    ASSERT_TRUE(place_for_dispatch(*node_, memory_, K_DEVICES));
    memory_.forget(&cell);
    memory_.adopt(&cell, 0, sizeof(cell), std::nullopt);

    ASSERT_TRUE(place_for_dispatch(*node_, memory_, K_DEVICES));

    EXPECT_EQ(node_->cached_queue_index_, 1);
    memory_.forget(&cell);
}

/** @brief Tests that a placement past the device count is refused, naming the index and the count, and nothing is cached. */
TEST_F(Small_DispatchPlacement, RefusesAPlacementPastTheDeviceCount)
{
    node_->place_on(K_DEVICES);

    const SushiRuntime::Result<void> placed = place_for_dispatch(*node_, memory_, K_DEVICES);

    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("device 2"), std::string::npos) << placed.error().message;
    EXPECT_NE(placed.error().message.find("2 device(s)"), std::string::npos) << placed.error().message;
    EXPECT_FALSE(node_->locality_checked_);
}

/** @brief Tests that a node whose regions sit on two devices is refused and nothing is cached. */
TEST_F(Small_DispatchPlacement, RefusesANodeWhoseRegionsSpanTwoDevices)
{
    int written = 0;
    int read = 0;
    memory_.adopt(&written, 0, sizeof(written), std::nullopt);
    memory_.adopt(&read, 1, sizeof(read), std::nullopt);
    node_->io_writes_.emplace_back(&written);
    node_->io_reads_.emplace_back(&read);

    const SushiRuntime::Result<void> placed = place_for_dispatch(*node_, memory_, K_DEVICES);

    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("span multiple device contexts"), std::string::npos)
        << placed.error().message;
    EXPECT_FALSE(node_->locality_checked_);
    memory_.forget(&written);
    memory_.forget(&read);
}
