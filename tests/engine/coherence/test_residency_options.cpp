/**************************************************************************/
/* test_residency_options.cpp                                             */
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
 * @file test_residency_options.cpp
 * @brief Tests how a planner given ResidencyOptions evicts, pins and keeps instances resident.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/coherence/logical_time_eviction.hpp>
#include <SushiRuntime/coherence/pin_set.hpp>
#include <SushiRuntime/coherence/plan_hash.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "support/fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::MemoryProbe;
using SushiRuntime::Internal::Memory::MemorySpace;
using SushiRuntime::Internal::Memory::ProbedMemory;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    const MemorySpaceID K_HOST(1);
    const MemorySpaceID K_DEVICE(2);
    constexpr std::size_t K_BYTES = 64;
    constexpr std::size_t K_HOST_CAPACITY = 1024;
    constexpr std::size_t K_DEVICE_CAPACITY = 2 * K_BYTES;

    /** @brief Reports a pool of a fixed size with every byte free. */
    class SizedMemoryProbe final : public MemoryProbe
    {
        public:
            explicit SizedMemoryProbe(std::size_t bytes) : bytes_(bytes)
            {

            }

            [[nodiscard]] ProbedMemory probe() const override
            {
                return ProbedMemory{bytes_, bytes_};
            }

        private:
            /// @brief Bytes the pool holds.
            std::size_t bytes_;
    };

    /**
     * @brief Holds three buffers of 64 bytes on a host of 1024 bytes and a device that holds two.
     *
     * Buffers a_, b_ and c_ start valid on the host only.
     */
    class Small_CoherencePlannerResidency : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                host_space_ = std::make_unique<DeviceMemorySpace>(
                    DeviceMemorySpaceDescription{K_HOST, 1, {}}, host_probe_);
                device_space_ = std::make_unique<DeviceMemorySpace>(
                    DeviceMemorySpaceDescription{K_DEVICE, 2, {}}, device_probe_);
                a_ = make_buffer(a_bytes_);
                b_ = make_buffer(b_bytes_);
                c_ = make_buffer(c_bytes_);
            }

            /** @brief Mints a buffer whose first valid instance is on the host. */
            GlobalHandle make_buffer(unsigned char* bytes)
            {
                const GlobalHandle handle = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
                EXPECT_EQ(directory_.describe(handle, K_BYTES, ElementLayout{4, 4}, K_HOST), HandleStatus::Ok);
                EXPECT_EQ(directory_.set_instance(handle, K_HOST, bytes), HandleStatus::Ok);
                return handle;
            }

            /** @brief Returns a planner over the host and the device with eviction by logical time. */
            CoherencePlanner planner(const PinSet* pins = nullptr,
                                     std::set<GlobalHandle> host_read = {})
            {
                CoherencePlanner subject(directory_, {host_space_.get(), device_space_.get()}, {&path_},
                                         uniform_transfer_cost());
                ResidencyOptions options;
                options.policy = &policy_;
                options.pins = pins;
                options.host = K_HOST;
                options.host_read = std::move(host_read);
                subject.set_residency(std::move(options));
                return subject;
            }

            /** @brief Returns a node on the device that reads and writes the buffers named. */
            NodeAccess node(std::vector<GlobalHandle> reads, std::vector<GlobalHandle> writes = {}) const
            {
                NodeAccess access;
                access.planned = true;
                access.space = K_DEVICE;
                for (GlobalHandle handle : reads)
                    access.reads.emplace_back(handle);
                for (GlobalHandle handle : writes)
                    access.writes.emplace_back(handle);
                return access;
            }

            /** @brief Returns the steps of plan that have kind. */
            static std::vector<PlannedStep> steps_of(const CopyPlan& plan, PlannedStepKind kind)
            {
                std::vector<PlannedStep> result;
                std::copy_if(plan.steps.begin(), plan.steps.end(), std::back_inserter(result),
                             [kind](const PlannedStep& step) { return step.kind == kind; });
                return result;
            }

            ResidencyDirectory directory_{0};
            SizedMemoryProbe host_probe_{K_HOST_CAPACITY};
            SizedMemoryProbe device_probe_{K_DEVICE_CAPACITY};
            std::unique_ptr<DeviceMemorySpace> host_space_;
            std::unique_ptr<DeviceMemorySpace> device_space_;
            FakeTransferPath path_;
            LogicalTimeEviction policy_;
            GlobalHandle a_;
            GlobalHandle b_;
            GlobalHandle c_;
            unsigned char a_bytes_[K_BYTES] = {};
            unsigned char b_bytes_[K_BYTES] = {};
            unsigned char c_bytes_[K_BYTES] = {};
    };

    TEST_F(Small_CoherencePlannerResidency, ACapacityTestEvictsTheInstanceReadFurthestAheadInGraphOrder)
    {
        CoherencePlanner subject = planner();
        // Reads a, b, c, a: when c needs room, a is read at node 3 and b only in the next step.
        const Result<CopyPlan> plan = subject.plan({node({a_}), node({b_}), node({c_}), node({a_})});
        ASSERT_TRUE(plan);

        const std::vector<PlannedStep> frees = steps_of(plan.value(), PlannedStepKind::Free);
        ASSERT_EQ(frees.size(), 1U);
        EXPECT_EQ(frees[0].handle, b_);
        EXPECT_EQ(frees[0].space, K_DEVICE);
        EXPECT_EQ(frees[0].node, 2U);
        EXPECT_TRUE(frees[0].evicts);
        EXPECT_EQ(steps_of(plan.value(), PlannedStepKind::Allocate).size(), 3U);
    }

    TEST_F(Small_CoherencePlannerResidency, TheEvictionPlanHashesEqualAcrossRuns)
    {
        const std::vector<NodeAccess> nodes{node({a_}), node({b_}), node({c_}), node({a_})};
        CoherencePlanner first = planner();
        CoherencePlanner second = planner();
        const Result<CopyPlan> one = first.plan(nodes);
        const Result<CopyPlan> two = second.plan(nodes);
        ASSERT_TRUE(one && two);
        EXPECT_EQ(one.value().steps, two.value().steps);
        EXPECT_EQ(plan_hash(one.value()), plan_hash(two.value()));
    }

    TEST_F(Small_CoherencePlannerResidency, AnInstanceHoldingTheOnlyValidCopyIsWrittenBackToTheHostBeforeItIsFreed)
    {
        CoherencePlanner subject = planner();
        // a is written on the device, so the host copy is stale; a is the furthest read when c needs room.
        const Result<CopyPlan> plan = subject.plan({node({}, {a_}), node({b_}), node({c_}), node({b_})});
        ASSERT_TRUE(plan);

        const std::vector<PlannedStep>& steps = plan.value().steps;
        const auto freed = std::find_if(steps.begin(), steps.end(), [](const PlannedStep& step)
        {
            return step.kind == PlannedStepKind::Free && step.evicts;
        });
        ASSERT_NE(freed, steps.end());
        EXPECT_EQ(freed->handle, a_);
        ASSERT_NE(freed, steps.begin());
        const PlannedStep& write_back = *std::prev(freed);
        EXPECT_EQ(write_back.kind, PlannedStepKind::Copy);
        EXPECT_EQ(write_back.handle, a_);
        EXPECT_EQ(write_back.source, K_DEVICE);
        EXPECT_EQ(write_back.space, K_HOST);
        EXPECT_TRUE(write_back.evicts);
    }

    TEST_F(Small_CoherencePlannerResidency, APinnedInstanceIsNeverEvicted)
    {
        PinSet pins;
        pins.pin(b_, K_DEVICE);
        CoherencePlanner subject = planner(&pins);
        // Without the pin b would go, as in the first test; with it a goes instead.
        const Result<CopyPlan> plan = subject.plan({node({a_}), node({b_}), node({c_}), node({a_})});
        ASSERT_TRUE(plan);

        const std::vector<PlannedStep> frees = steps_of(plan.value(), PlannedStepKind::Free);
        ASSERT_EQ(frees.size(), 1U);
        EXPECT_EQ(frees[0].handle, a_);
    }

    TEST_F(Small_CoherencePlannerResidency, ASpaceWhoseInstancesAreAllPinnedRefusesTheAllocation)
    {
        PinSet pins;
        pins.pin(a_, K_DEVICE);
        pins.pin(b_, K_DEVICE);
        CoherencePlanner subject = planner(&pins);

        const Result<CopyPlan> plan = subject.plan({node({a_}), node({b_}), node({c_})});
        ASSERT_FALSE(plan);
        EXPECT_EQ(plan.error().code, ErrorCode::AllocationFailed);
    }

    TEST_F(Small_CoherencePlannerResidency, AnInstanceTheCurrentNodeReadsIsNotEvictedForItself)
    {
        CoherencePlanner subject = planner();
        // One node reads three buffers of a device that holds two: the third has no victim.
        const Result<CopyPlan> plan = subject.plan({node({a_, b_, c_})});
        ASSERT_FALSE(plan);
        EXPECT_EQ(plan.error().code, ErrorCode::AllocationFailed);
    }

    TEST_F(Small_CoherencePlannerResidency, WithinCapacityInstancesStayResidentAtTheGraphEnd)
    {
        CoherencePlanner subject = planner();
        const Result<CopyPlan> plan = subject.plan({node({a_}), node({b_})});
        ASSERT_TRUE(plan);

        EXPECT_TRUE(steps_of(plan.value(), PlannedStepKind::Free).empty());
        EXPECT_EQ(steps_of(plan.value(), PlannedStepKind::Allocate).size(), 2U);
        EXPECT_TRUE(plan.value().steady);
        EXPECT_FALSE(std::any_of(plan.value().steps.begin(), plan.value().steps.end(),
                                 [](const PlannedStep& step) { return step.node == PlannedStep::K_GRAPH_END; }));
    }

    TEST_F(Small_CoherencePlannerResidency, AWriteTheHostDoesNotReadLeavesNoCopyBackAndAPlanThatIsNotSteady)
    {
        CoherencePlanner subject = planner();
        const Result<CopyPlan> plan = subject.plan({node({}, {a_})});
        ASSERT_TRUE(plan);

        EXPECT_FALSE(plan.value().steady);
        EXPECT_TRUE(steps_of(plan.value(), PlannedStepKind::Copy).empty());
    }

    TEST_F(Small_CoherencePlannerResidency, ABufferTheHostReadsBetweenStepsIsCopiedBackToItsHome)
    {
        CoherencePlanner subject = planner(nullptr, {a_});
        const Result<CopyPlan> plan = subject.plan({node({}, {a_})});
        ASSERT_TRUE(plan);

        const std::vector<PlannedStep> copies = steps_of(plan.value(), PlannedStepKind::Copy);
        ASSERT_EQ(copies.size(), 1U);
        EXPECT_EQ(copies[0].handle, a_);
        EXPECT_EQ(copies[0].source, K_DEVICE);
        EXPECT_EQ(copies[0].space, K_HOST);
        EXPECT_EQ(copies[0].node, PlannedStep::K_GRAPH_END);
        EXPECT_FALSE(copies[0].evicts);
        EXPECT_TRUE(plan.value().steady);
    }

    TEST_F(Small_CoherencePlannerResidency, ASecondPlanStartsFromTheStateTheFirstLeft)
    {
        CoherencePlanner subject = planner();
        const std::vector<NodeAccess> nodes{node({a_}), node({b_})};
        ASSERT_TRUE(subject.plan(nodes));
        // The instances sit on the device after the first run, so the second plan has nothing to add.
        ASSERT_EQ(directory_.set_instance(a_, K_DEVICE, &a_bytes_[0]), HandleStatus::Ok);
        ASSERT_EQ(directory_.set_instance(b_, K_DEVICE, &b_bytes_[0]), HandleStatus::Ok);

        const Result<CopyPlan> second = subject.plan(nodes);
        ASSERT_TRUE(second);
        EXPECT_TRUE(second.value().steps.empty());
    }

    TEST_F(Small_CoherencePlannerResidency, APlannerWithoutOptionsKeepsFreeingEveryInstanceItMade)
    {
        CoherencePlanner subject(directory_, {host_space_.get(), device_space_.get()}, {&path_},
                                 uniform_transfer_cost());
        const Result<CopyPlan> plan = subject.plan({node({a_}), node({b_}), node({c_})});
        ASSERT_TRUE(plan);

        EXPECT_EQ(steps_of(plan.value(), PlannedStepKind::Free).size(), 3U);
        EXPECT_TRUE(plan.value().steady);
    }
}
