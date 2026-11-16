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
#include "support/handle_table.hpp"

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

        /// @brief Host memory of two devices; it answers which devices share a context.
        SushiRuntime::Internal::Memory::USMAllocator memory_{
            SushiRuntime::Internal::Memory::make_host_usm_source(K_DEVICES)};
        /// @brief Gives each address of a test a handle that resolves to it on one device.
        TestSupport::HandleTable handles_;
        /// @brief The node placed.
        sushi_ptr<Node> node_ = make_sushi<Node>();
};

/** @brief Tests that a node with no region and nothing stated is placed on device 0 and marked placed. */
TEST_F(Small_DispatchPlacement, CachesTheDefaultDeviceOfARegionlessNode)
{
    ASSERT_TRUE(place_for_dispatch(*node_, memory_, handles_.resolver(), K_DEVICES));

    EXPECT_EQ(node_->cached_queue_index_, 0);
    EXPECT_TRUE(node_->locality_checked_);
}

/** @brief Tests that the device the placement rule picks, the owner of the node's region, is cached. */
TEST_F(Small_DispatchPlacement, CachesTheOwnerOfTheNodesRegion)
{
    int cell = 0;
    node_->io_writes_.emplace_back(handles_.place(&cell, 1));

    ASSERT_TRUE(place_for_dispatch(*node_, memory_, handles_.resolver(), K_DEVICES));

    EXPECT_EQ(node_->cached_queue_index_, 1);
}

/** @brief Tests that a placed node keeps its device when its region's owner changes before a later dispatch. */
TEST_F(Small_DispatchPlacement, APlacedNodeKeepsItsDeviceOnALaterDispatch)
{
    int cell = 0;
    node_->io_writes_.emplace_back(handles_.place(&cell, 1));
    ASSERT_TRUE(place_for_dispatch(*node_, memory_, handles_.resolver(), K_DEVICES));
    ASSERT_EQ(handles_.retire(&cell), SushiRuntime::HandleStatus::Ok);
    node_->io_writes_.front() = SushiRuntime::BufferRegion(handles_.place(&cell, 0));

    ASSERT_TRUE(place_for_dispatch(*node_, memory_, handles_.resolver(), K_DEVICES));

    EXPECT_EQ(node_->cached_queue_index_, 1);
}

/** @brief Tests that a placement past the device count is refused, naming the index and the count, and nothing is cached. */
TEST_F(Small_DispatchPlacement, RefusesAPlacementPastTheDeviceCount)
{
    node_->place_on(K_DEVICES);

    const SushiRuntime::Result<void> placed = place_for_dispatch(*node_, memory_, handles_.resolver(), K_DEVICES);

    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("device 2"), std::string::npos) << placed.error().message;
    EXPECT_NE(placed.error().message.find("2 device(s)"), std::string::npos) << placed.error().message;
    EXPECT_FALSE(node_->locality_checked_);
}

/** @brief Tests that a rebindable node whose regions sit on two devices runs on the device of its anchor, the written region. */
TEST_F(Small_DispatchPlacement, PlacesARebindableNodeWhoseRegionsSpanTwoDevicesOnItsAnchor)
{
    int written = 0;
    int read = 0;
    node_->kernel_addressing = SushiRuntime::Internal::Graph::KernelAddressing::Rebindable;
    node_->io_writes_.emplace_back(handles_.place(&written, 0));
    node_->io_reads_.emplace_back(handles_.place(&read, 1));

    ASSERT_TRUE(place_for_dispatch(*node_, memory_, handles_.resolver(), K_DEVICES));

    EXPECT_EQ(node_->cached_queue_index_, 0);
    EXPECT_TRUE(node_->locality_checked_);
}
