/**************************************************************************/
/* test_dynamic_graph_placement.cpp                                       */
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
 * @file test_dynamic_graph_placement.cpp
 * @brief Tests that the placement rules place the nodes of the regions of a DynamicGraph.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Tests that the placement rules place the nodes of a DynamicGraph's regions. */
namespace DynamicGraphPlacementTests
{
    namespace
    {
        /** @brief Returns a rule set whose one rule sends class "dynamic.ruled" to a device. */
        PlacementRules rule_to_device(std::size_t device, AbsentAction if_absent)
        {
            PlacementRule rule;
            rule.name = "ruled";
            rule.match.placement_class = "dynamic.ruled";
            rule.target.device_index = device;
            rule.if_absent = AbsentPolicy{if_absent, std::chrono::milliseconds{0}};
            PlacementRules rules;
            EXPECT_TRUE(rules.add(std::move(rule)));
            return rules;
        }

        /** @brief Records in region 0, in the rule's scope, a task that notes its device. */
        void record_ruled_task(const Runtime& rt, DynamicGraph& world,
                               std::optional<sycl::device>& ran_on)
        {
            world.region(0).recorder().within(
                rt.placement_scope("dynamic.ruled"),
                [&ran_on](Recorder& in_scope)
                {
                    in_scope.task(Access(),
                                  [&ran_on](sycl::queue& queue, const std::vector<sycl::event>&)
                                  {
                                      ran_on = queue.get_device();
                                      return sycl::event{};
                                  });
                });
        }
    } // namespace

    /** @brief Fixture of the placement test whose run is refused before any body runs, on a host runtime. */
    class Small_DynamicGraphPlacement : public TestSupport::HostRuntimeTest
    {
        protected:
            /** @brief Returns the number of devices the runtime drives, which is the first index it lacks. */
            [[nodiscard]] std::size_t absent_device() { return runtime().advanced().device_count(); }
    };

    /**
     * @brief Fixture of the placement tests whose task body runs on a device; it hands the program's runtime back with no rules.
     */
    class Medium_DynamicGraphPlacement : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Returns the number of devices the runtime drives, which is the first index it lacks. */
            [[nodiscard]] std::size_t absent_device() const { return runtime().advanced().device_count(); }

            /** @brief Returns the device the runtime submits the work of device index @p index on. */
            [[nodiscard]] sycl::device device_of(std::size_t index) const
            {
                return runtime().advanced().queue(DeviceIndex{index}).get_device();
            }

            /** @brief Clears the rules, so no later test places a node by this test's rule. */
            void TearDown() override { (void)runtime().set_placement_rules(PlacementRules{}); }
    };

    /** @brief Tests that a Fail rule naming an absent device refuses the run of a region's node. */
    TEST_F(Small_DynamicGraphPlacement, ARuleToAnAbsentDeviceRefusesTheRun)
    {
        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Fail)));
        const auto refused = world.run();

        ASSERT_FALSE(refused) << "the region's node is placed by the Fail rule";
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("\"ruled\""), std::string::npos)
            << refused.error().message;
    }

    /** @brief Tests that a rule change between two runs re-places a region's node without a re-record. */
    TEST_F(Medium_DynamicGraphPlacement, ARuleChangeReplacesARegionsNodeWithoutARecord)
    {
        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Fail)));
        ASSERT_FALSE(world.run());

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Local)));
        ASSERT_TRUE(world.run()) << "the new set reaches the region without a re-record";
        ASSERT_TRUE(noted.has_value());
        EXPECT_EQ(*noted, device_of(0));
    }

    /** @brief Tests that a rule set that has not changed composes nothing at the next run. */
    TEST_F(Medium_DynamicGraphPlacement, AnUnchangedRuleSetComposesNothing)
    {
        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Local)));
        ASSERT_TRUE(world.run());
        const std::size_t composed = world.compose_count();

        ASSERT_TRUE(world.run());
        EXPECT_EQ(world.compose_count(), composed) << "an unchanged set composes nothing";
    }

    /** @brief Tests that a rule change to device one re-places the node there and composes once. */
    TEST_F(Medium_DynamicGraphPlacement, ARuleChangeToDeviceOneRemapsTheNode)
    {
        if (absent_device() < 2)
            GTEST_SKIP() << "needs a second device to remap the node to";

        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Local)));
        ASSERT_TRUE(world.run());
        const std::size_t composed = world.compose_count();

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(1, AbsentAction::Fail)));
        ASSERT_TRUE(world.run());
        EXPECT_EQ(world.compose_count(), composed + 1);
        ASSERT_TRUE(noted.has_value());
        EXPECT_EQ(*noted, device_of(1));
    }
} // namespace DynamicGraphPlacementTests
