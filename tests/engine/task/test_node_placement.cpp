/**************************************************************************/
/* test_node_placement.cpp                                                */
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
 * @file test_node_placement.cpp
 * @brief Tests the placement a node states and the device placement_of names for it.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/in_memory_placement_memory.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>
#include "support/handle_table.hpp"

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Internal::Graph::Node;
    using SushiRuntime::Internal::Graph::placement_of;
} // namespace

/** @brief Holds memory of two devices that share one SYCL context; a test records ownership with record(). */
class Small_NodePlacementRule : public ::testing::Test
{
    protected:
        /// @brief Two devices in context 0.
        SushiRuntime::Internal::Graph::InMemoryPlacementMemory memory_{std::vector<std::size_t>{0, 0}};
};

/** @brief Holds memory of two devices that sit in two SYCL contexts; a test records ownership with record(). */
class Small_NodePlacementContexts : public ::testing::Test
{
    protected:
        /// @brief Device 0 in context 0 and device 1 in context 1.
        SushiRuntime::Internal::Graph::InMemoryPlacementMemory memory_{std::vector<std::size_t>{0, 1}};
};

/** @brief Tests that place_on stops the process on an index it cannot hold, and the earlier placement stays. A death test starts a child process. */
TEST(Medium_NodePlacementStateDeathTest, PlaceOnStopsOnAnIndexItCannotHold)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->place_on(1);

    const std::size_t too_large = Node::K_NO_PLACEMENT;
    EXPECT_DEATH(node->place_on(too_large),
                 "Node::place_on." + std::to_string(too_large) + ".: a device index must be below");
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(1));
}

/** @brief Tests that a node with no region and nothing stated runs on the default device, 0. */
TEST_F(Small_NodePlacementRule, ANodeWithNoRegionRunsOnTheDefaultWhenNothingIsStated)
{
    const sushi_ptr<Node> node = make_sushi<Node>();

    EXPECT_EQ(placement_of(*node, memory_).value(), SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT);
    EXPECT_EQ(SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT, 0u);
}

/** @brief Tests that a node with no region runs on the device it states. */
TEST_F(Small_NodePlacementRule, ANodeWithNoRegionRunsOnItsStatedDevice)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->place_on(1);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}

/** @brief Tests that a node whose only region is a read runs on the owner of that region. */
TEST_F(Small_NodePlacementRule, ANodeWithOnlyAReadRegionRunsOnItsOwner)
{
    int read_cell = 0;
    memory_.record(TestSupport::region_at(&read_cell).base, 1);
    const sushi_ptr<Node> reader = make_sushi<Node>();
    reader->io_reads_.emplace_back(TestSupport::region_at(&read_cell));

    EXPECT_EQ(placement_of(*reader, memory_).value(), 1u);
}

/** @brief Tests that the owner of the first write outranks the owner of the first read. */
TEST_F(Small_NodePlacementRule, TheFirstWriteRegionOutranksTheFirstReadRegion)
{
    int read_cell = 0;
    int write_cell = 0;
    memory_.record(TestSupport::region_at(&read_cell).base, 0);
    memory_.record(TestSupport::region_at(&write_cell).base, 1);
    const sushi_ptr<Node> both = make_sushi<Node>();
    both->io_reads_.emplace_back(TestSupport::region_at(&read_cell));
    both->io_writes_.emplace_back(TestSupport::region_at(&write_cell));

    EXPECT_EQ(placement_of(*both, memory_).value(), 1u);
}

/** @brief Tests that an owned region places a node that states nothing. */
TEST_F(Small_NodePlacementRule, AnOwnedRegionPlacesANodeThatStatesNothing)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}

/** @brief Tests that a rebindable node's stated device in the owner's context outranks the owner. */
TEST_F(Small_NodePlacementRule, ARebindableNodesStatedDeviceInTheOwnersContextOutranksTheOwner)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    node->kernel_addressing = SushiRuntime::Internal::Graph::KernelAddressing::Rebindable;
    node->place_on(0);

    EXPECT_EQ(placement_of(*node, memory_).value(), 0u);
}

/** @brief Tests that a fixed node runs on another device of the owner's context when its memory is host visible. */
TEST_F(Small_NodePlacementRule, AFixedNodeOverHostVisibleMemoryRunsOnAnotherDeviceOfTheOwnersContext)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    node->place_on(0);

    EXPECT_EQ(placement_of(*node, memory_).value(), 0u) << "its raw addresses hold on every device of the context";
}

/** @brief Tests that a fixed node is refused on a sibling device when one of its regions is device USM of another device. */
TEST_F(Small_NodePlacementRule, AFixedNodeOverDeviceUSMIsRefusedOnASiblingDevice)
{
    int shared_cell = 0;
    int device_cell = 0;
    memory_.record(TestSupport::region_at(&shared_cell).base, 0);
    memory_.record(TestSupport::region_at(&device_cell).base, 1);
    memory_.record_device_only(TestSupport::region_at(&device_cell).base);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&shared_cell));
    node->io_reads_.emplace_back(TestSupport::region_at(&device_cell));
    node->place_on(1);
    const sushi_ptr<Node> elsewhere = make_sushi<Node>();
    elsewhere->io_writes_.emplace_back(TestSupport::region_at(&device_cell));
    elsewhere->place_on(0);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u) << "the device USM lives on the stated device";
    const SushiRuntime::Result<std::size_t> placed = placement_of(*elsewhere, memory_);
    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("device 1 as device USM"), std::string::npos) << placed.error().message;
}

/** @brief Tests that a fixed node's stated device in another SYCL context than the owner's is refused, naming the owner. */
TEST_F(Small_NodePlacementContexts, AFixedNodeIsRefusedOnAStatedDeviceInAnotherContext)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    node->place_on(0);

    const SushiRuntime::Result<std::size_t> placed = placement_of(*node, memory_);

    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("device 1 in another SYCL context"), std::string::npos) << placed.error().message;
    EXPECT_NE(placed.error().message.find("raw addresses"), std::string::npos) << placed.error().message;
}

/** @brief Tests that a fixed node stating nothing is refused on its owner when another region lies in another context. */
TEST_F(Small_NodePlacementContexts, AFixedNodeOverTwoContextsIsRefusedOnItsOwner)
{
    int written = 0;
    int read = 0;
    memory_.record(TestSupport::region_at(&written).base, 1);
    memory_.record(TestSupport::region_at(&read).base, 0);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&written));
    node->io_reads_.emplace_back(TestSupport::region_at(&read));

    const SushiRuntime::Result<std::size_t> placed = placement_of(*node, memory_);

    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("device 0 in another SYCL context"), std::string::npos) << placed.error().message;
}

/** @brief Tests that a rebindable node stating nothing runs on its owner over two contexts, since the planner copies. */
TEST_F(Small_NodePlacementContexts, ARebindableNodeOverTwoContextsRunsOnItsOwner)
{
    int written = 0;
    int read = 0;
    memory_.record(TestSupport::region_at(&written).base, 1);
    memory_.record(TestSupport::region_at(&read).base, 0);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&written));
    node->io_reads_.emplace_back(TestSupport::region_at(&read));
    node->kernel_addressing = SushiRuntime::Internal::Graph::KernelAddressing::Rebindable;

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}

/** @brief Tests that a rebindable node is placed on a stated device in another context than the owner's. */
TEST_F(Small_NodePlacementContexts, ARebindableNodeIsPlacedOnAStatedDeviceInAnotherContext)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    node->kernel_addressing = SushiRuntime::Internal::Graph::KernelAddressing::Rebindable;
    node->place_on(0);

    EXPECT_EQ(placement_of(*node, memory_).value(), 0u) << "the planner copies the region to device 0";
}

/** @brief Tests that a pointer table alone does not make a node rebindable: the marking is explicit. */
TEST_F(Small_NodePlacementContexts, APointerTableAloneLeavesTheNodeFixed)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    SushiRuntime::Internal::Memory::ResidencyDirectory directory(0);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    node->kernel_pointers_ = std::make_unique<SushiRuntime::Internal::Graph::KernelPointers>(
        std::vector<SushiRuntime::Internal::Graph::KernelPointerSlot>{}, directory);
    node->place_on(0);

    const SushiRuntime::Result<std::size_t> placed = placement_of(*node, memory_);

    ASSERT_FALSE(placed) << "a dynamic node binds its device slots through a table and stays fixed";
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
}

/** @brief Tests that stating the owner itself is accepted, whatever the contexts. */
TEST_F(Small_NodePlacementContexts, StatingTheOwnerItselfIsAccepted)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    node->place_on(1);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u) << "the owner itself is never refused";
}

/** @brief Tests that a stated index past the devices is returned as stated, for the dispatch to refuse. */
TEST_F(Small_NodePlacementContexts, AStatedIndexPastTheDevicesIsLeftToTheDispatch)
{
    int cell = 0;
    memory_.record(TestSupport::region_at(&cell).base, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    node->place_on(5);

    EXPECT_EQ(placement_of(*node, memory_).value(), 5u);
}

/** @brief Tests that an unowned region with nothing stated runs on device 0. */
TEST_F(Small_NodePlacementRule, AnUnownedRegionWithNothingStatedRunsOnDeviceZero)
{
    int cell = 0;
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_reads_.emplace_back(TestSupport::region_at(&cell));

    EXPECT_EQ(placement_of(*node, memory_).value(), 0u);
}

/** @brief Tests that a stated device stands for an unowned region. */
TEST_F(Small_NodePlacementRule, AnUnownedRegionLeavesTheStatedPlacementInForce)
{
    int cell = 0;
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_reads_.emplace_back(TestSupport::region_at(&cell));
    node->place_on(1);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}

/** @brief Holds a residency directory and an allocator over two host devices, as a runtime gives placement_of(). */
class Small_NodePlacementPlanned : public ::testing::Test
{
    protected:
        /** @brief Mints a planned buffer: described with the host as its home and no instance anywhere. */
        SushiRuntime::GlobalHandle mint_planned()
        {
            const SushiRuntime::MintOutcome minted =
                directory_.mint(SushiRuntime::HandleKind::Buffer, SushiRuntime::HandleFlags::None);
            EXPECT_EQ(minted.status, SushiRuntime::HandleStatus::Ok);
            EXPECT_EQ(directory_.describe(minted.handle, sizeof(int), {sizeof(int), alignof(int)},
                                          SushiRuntime::MemorySpaceID(0)),
                      SushiRuntime::HandleStatus::Ok);
            return minted.handle;
        }

        /// @brief Allocates nothing.
        SushiRuntime::Internal::Memory::USMAllocator allocator_{SushiRuntime::Internal::Memory::make_host_usm_source(2)};
        /// @brief Holds the planned buffers.
        SushiRuntime::Internal::Memory::ResidencyDirectory directory_{1};
};

/** @brief Tests that a node over planned buffers with no instance anywhere runs on the default device. */
TEST_F(Small_NodePlacementPlanned, ANodeOverPlannedBuffersWithNoInstanceRunsOnTheDefaultDevice)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_reads_.emplace_back(mint_planned());
    node->io_writes_.emplace_back(mint_planned());

    EXPECT_EQ(placement_of(*node, allocator_, directory_).value(),
              SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT);
}

/** @brief Tests that a planned buffer the host wrote, whose one instance is on the host, still places its node on the default device. */
TEST_F(Small_NodePlacementPlanned, APlannedBufferHeldOnlyByTheHostRunsOnTheDefaultDevice)
{
    int host_bytes = 0;
    const SushiRuntime::GlobalHandle planned = mint_planned();
    ASSERT_EQ(directory_.set_instance(planned, SushiRuntime::MemorySpaceID(0), &host_bytes),
              SushiRuntime::HandleStatus::Ok);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(planned);

    EXPECT_EQ(placement_of(*node, allocator_, directory_).value(),
              SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT);
}

namespace
{
    /** @brief Memory whose recorded_device() knows nothing while owning_device() names device 1, as an unrecorded USM buffer does. */
    class UnrecordedOwnerMemory final : public SushiRuntime::Internal::Graph::PlacementMemory
    {
        public:
            [[nodiscard]] std::optional<std::size_t> recorded_device(SushiRuntime::GlobalHandle) const override
            {
                return std::nullopt;
            }

            [[nodiscard]] std::optional<std::size_t> owning_device(SushiRuntime::GlobalHandle handle) const override
            {
                if (handle.is_null())
                    return std::nullopt;
                return 1;
            }

            [[nodiscard]] std::size_t device_count() const override
            {
                return 2;
            }

            [[nodiscard]] bool shares_context(std::size_t, std::size_t) const override
            {
                return true;
            }

            [[nodiscard]] bool host_visible(SushiRuntime::GlobalHandle) const override
            {
                return false;
            }
    };
} // namespace

/** @brief Tests that anchor_device names the owner placement_of runs the node on, even when no record exists (the case the scheduler copy missed). */
TEST(Small_NodePlacementAnchor, AnchorDeviceAgreesWithPlacementOfForAnUnrecordedOwner)
{
    int cell = 0;
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(TestSupport::region_at(&cell));
    const UnrecordedOwnerMemory memory;

    EXPECT_EQ(SushiRuntime::Internal::Graph::anchor_of(*node), node->io_writes_.front().base);
    EXPECT_EQ(SushiRuntime::Internal::Graph::anchor_device(*node, memory), std::optional<std::size_t>(1));
    EXPECT_EQ(placement_of(*node, memory).value(), 1u);
}
