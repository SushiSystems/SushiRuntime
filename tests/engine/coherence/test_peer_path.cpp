/**************************************************************************/
/* test_peer_path.cpp                                                     */
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
 * @file test_peer_path.cpp
 * @brief Tests that PeerPath connects spaces of two contexts that read each other, and that the report names the path of each planned copy.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/coherence/directory_planned_node_factory.hpp>
#include <SushiRuntime/coherence/peer_path.hpp>
#include <SushiRuntime/coherence/same_context_path.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "fake_transfer_path.hpp"
#include "transfer_path_doubles.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);
    const MemorySpaceID K_DEVICE_2(3);
    constexpr std::size_t K_BYTES = 8;

    /** @brief Holds device 0 and 1 in one context and device 2 in another. */
    class Medium_PeerPath : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                if (!queues_.available())
                    GTEST_SKIP() << "SYCL offers no CPU device.";
            }

            FixedMemoryProbe probe_;
            DeviceMemorySpace device_0_{DeviceMemorySpaceDescription{K_DEVICE_0, 1, {K_DEVICE_1}}, probe_};
            DeviceMemorySpace device_1_{DeviceMemorySpaceDescription{K_DEVICE_1, 2, {K_DEVICE_0}}, probe_};
            DeviceMemorySpace device_2_{DeviceMemorySpaceDescription{K_DEVICE_2, 3, {}}, probe_};
            CPUSpaceQueues queues_;
            FixedPeerAccess granted_{true};
            FixedPeerAccess refused_{false};
            RecordingTransferObserver observer_;
    };

    TEST_F(Medium_PeerPath, ConnectsTwoContextsOnlyWhenAccessIsGranted)
    {
        const PeerPath open(queues_, granted_);
        const PeerPath closed(queues_, refused_);

        EXPECT_TRUE(open.connects(device_0_, device_2_));
        EXPECT_FALSE(closed.connects(device_0_, device_2_));
        EXPECT_EQ(granted_.asked(), 1);
        EXPECT_EQ(refused_.asked(), 1);
    }

    TEST_F(Medium_PeerPath, LeavesTwoSpacesOfOneContextToTheSameContextPath)
    {
        const PeerPath subject(queues_, granted_);

        EXPECT_FALSE(subject.connects(device_0_, device_1_));
        EXPECT_EQ(granted_.asked(), 0);
    }

    TEST_F(Medium_PeerPath, CopiesTheBytesAndReportsItsNameForTheCopy)
    {
        PeerPath subject(queues_, granted_, &observer_);
        const std::array<unsigned char, 5> from{5, 4, 3, 2, 1};
        std::array<unsigned char, 5> to{};

        subject.copy(TransferRequest{K_DEVICE_0, K_DEVICE_2, from.data(), to.data(), to.size()}, {}).wait();

        EXPECT_EQ(to, from);
        ASSERT_EQ(observer_.heard().size(), 1U);
        EXPECT_EQ(observer_.heard()[0].path, "peer");
        EXPECT_EQ(observer_.heard()[0].source, K_DEVICE_0);
        EXPECT_EQ(observer_.heard()[0].destination, K_DEVICE_2);
    }

    TEST_F(Medium_PeerPath, EachPlannedCopyTakesItsPathAndTheReportNamesIt)
    {
        SameContextPath same_context(queues_, &observer_);
        PeerPath peer(queues_, granted_, &observer_);
        ResidencyDirectory directory(0);
        const GlobalHandle buffer = directory.mint(HandleKind::Buffer, HandleFlags::None).handle;
        std::array<unsigned char, K_BYTES> on_0{1, 2, 3, 4, 5, 6, 7, 8};
        std::array<unsigned char, K_BYTES> on_1{};
        std::array<unsigned char, K_BYTES> on_2{};
        ASSERT_EQ(directory.describe(buffer, K_BYTES, ElementLayout{1, 1}, K_DEVICE_0), HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(buffer, K_DEVICE_0, on_0.data()), HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(buffer, K_DEVICE_1, on_1.data()), HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(buffer, K_DEVICE_2, on_2.data()), HandleStatus::Ok);

        const std::vector<TransferPath*> paths{&same_context, &peer};
        CoherencePlanner planner(directory, {&device_0_, &device_1_, &device_2_}, {&same_context, &peer},
                                 uniform_transfer_cost());
        auto access = [buffer](MemorySpaceID space, bool writes)
        {
            NodeAccess node;
            node.planned = true;
            node.space = space;
            (writes ? node.writes : node.reads).emplace_back(buffer);
            return node;
        };
        const Result<CopyPlan> plan =
            planner.plan({access(K_DEVICE_0, true), access(K_DEVICE_1, false), access(K_DEVICE_2, false)});
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan.value().steps.size(), 2U);

        DirectoryPlannedNodeFactory factory(directory, nullptr, paths);
        sycl::queue* const queue = queues_.queue_of(K_DEVICE_0);
        for (const PlannedStep& step : plan.value().steps)
        {
            ASSERT_EQ(step.kind, PlannedStepKind::Copy);
            Result<sushi_ptr<Internal::Graph::Node>> node = factory.make(step);
            ASSERT_TRUE(node);
            node.value()->host_work(*queue, {}).wait();
        }

        EXPECT_EQ(on_1, on_0);
        EXPECT_EQ(on_2, on_0);
        const std::vector<RecordingTransferObserver::Heard> heard = observer_.heard();
        ASSERT_EQ(heard.size(), 2U);
        EXPECT_EQ(heard[0].path, "same_context");
        EXPECT_EQ(heard[0].destination, K_DEVICE_1);
        EXPECT_EQ(heard[1].path, "peer");
        EXPECT_EQ(heard[1].destination, K_DEVICE_2);
    }
} // namespace
