/**************************************************************************/
/* test_recorder_placement.cpp                                            */
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
 * @file test_recorder_placement.cpp
 * @brief Tests how a Recorder places the nodes it records: placement scopes, placed tasks and placement rules.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "recorder_fixtures.hpp"

/** @brief The untracked verbs a Fail rule refuses, as the rows of one test. */
enum class Verb
{
    CountKernel,  /**< untracked_kernel() over a count. */
    RawKernel     /**< untracked_raw_kernel(). */
};

/** @brief Fixture of the rows over the untracked verbs. */
class Small_RecorderPlacementScopeVerb : public RecorderHostTest,
                                         public ::testing::WithParamInterface<Verb>
{
};

using namespace SushiRuntime;

namespace PlacementScopeRecordingTests
{
    namespace
    {
        /** @brief Records one host node that reads and writes nothing. */
        void record_idle(Recorder& recorder, const NodeOptions& options = {})
        {
            recorder.host(Access(Reads(), Writes()), [] {}, options);
        }
    } // namespace

    /** @brief Records the seven nodes the scope tests read: before every scope, in the outer, in the inner (twice), in the outer again (twice), after every scope. */
    void record_nested_scopes(const Runtime& rt, Graph& graph)
    {
        Recorder& recorder = graph.recorder();
        const PlacementScope outer = rt.placement_scope("outer.group", 7u);
        const PlacementScope inner = rt.placement_scope("inner.group", 0u);

        NodeOptions own_class_and_key;
        own_class_and_key.placement_class = "own.group"_class;
        own_class_and_key.placement_key = 3u;
        NodeOptions own_class_without_key;
        own_class_without_key.placement_class = "own.group"_class;

        record_idle(recorder);
        recorder.within(outer, [&](Recorder& in_outer)
        {
            record_idle(in_outer);
            in_outer.within(inner, [&](Recorder& in_inner)
            {
                record_idle(in_inner);
                record_idle(in_inner, own_class_and_key);
            });
            record_idle(in_outer);
            record_idle(in_outer, own_class_without_key);
        });
        record_idle(recorder);
    }

    /** @brief Runs the RecorderPlacementScope tests on a host runtime. */
    class Small_RecorderPlacementScope : public RecorderHostTest
    {
    };

    /** @brief Tests that nodes take the innermost open scope, and none outside every scope. */
    TEST_F(Small_RecorderPlacementScope, NestedScopesStampTheInnermostClassAndKey)
    {
        auto graph = runtime().graph();
        record_nested_scopes(runtime(), graph);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 7u);

        EXPECT_FALSE(plan->nodes[0].placement_class.is_named()) << "before every scope";
        EXPECT_FALSE(plan->nodes[0].placement_key.has_value());

        EXPECT_EQ(plan->nodes[1].placement_class, "outer.group"_class);
        ASSERT_TRUE(plan->nodes[1].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[1].placement_key, 7u);

        EXPECT_EQ(plan->nodes[2].placement_class, "inner.group"_class);
        ASSERT_TRUE(plan->nodes[2].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[2].placement_key, 0u) << "a key of 0 is a key";

        EXPECT_FALSE(plan->nodes[6].placement_class.is_named()) << "after every scope";
        EXPECT_FALSE(plan->nodes[6].placement_key.has_value());
    }

    /** @brief Tests that a node's own class and key beat the scope's. */
    TEST_F(Small_RecorderPlacementScope, ANodesOwnClassAndKeyBeatTheScopes)
    {
        auto graph = runtime().graph();
        record_nested_scopes(runtime(), graph);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 7u);

        EXPECT_EQ(plan->nodes[3].placement_class, "own.group"_class);
        ASSERT_TRUE(plan->nodes[3].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[3].placement_key, 3u) << "the own key beats the scope key";

        EXPECT_EQ(plan->nodes[5].placement_class, "own.group"_class);
        EXPECT_FALSE(plan->nodes[5].placement_key.has_value())
            << "a node that names a class keeps a none key; the scope key is not borrowed";
    }

    /** @brief Tests that closing an inner scope restores the outer one. */
    TEST_F(Small_RecorderPlacementScope, ClosingAScopeRestoresTheOuterOne)
    {
        auto graph = runtime().graph();
        record_nested_scopes(runtime(), graph);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 7u);

        EXPECT_EQ(plan->nodes[4].placement_class, "outer.group"_class)
            << "after the inner scope closed";
        ASSERT_TRUE(plan->nodes[4].placement_key.has_value());
        EXPECT_EQ(*plan->nodes[4].placement_key, 7u);
    }

    /** @brief Tests that within() returns the callable result. */
    TEST_F(Small_RecorderPlacementScope, WithinReturnsWhatTheCallableReturns)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();

        const std::size_t rule = recorder.within(
            rt.placement_scope("a.b"),
            [](Recorder& in_scope)
            {
                return in_scope.host(Access(Reads(), Writes()), [] {});
            });

        EXPECT_EQ(rule, graph.last_rule());
    }

    /** @brief Tests that a callable that throws leaves no scope open on the recorder. */
    TEST_F(Small_RecorderPlacementScope, AThrowingCallableClosesItsScope)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();

        EXPECT_THROW(recorder.within(rt.placement_scope("a.b", 1u),
                                     [](Recorder&) { throw std::runtime_error("unwound"); }),
                     std::runtime_error);
        record_idle(recorder);

        const auto plan = graph.plan();
        ASSERT_TRUE(plan);
        ASSERT_EQ(plan->nodes.size(), 1u);
        EXPECT_FALSE(plan->nodes[0].placement_class.is_named());
        EXPECT_FALSE(plan->nodes[0].placement_key.has_value());
    }
} // namespace PlacementScopeRecordingTests

namespace PlacementScopeDeviceRecordingTests
{
    namespace
    {
        /** @brief Holds the class every rule of this file matches. */
        constexpr const char* K_CLASS = "recorder.device";

        /** @brief Holds the element count of every launch of this file. */
        constexpr std::size_t K_COUNT = 64;

        /** @brief Returns a rule set whose one rule sends K_CLASS to a device index. */
        PlacementRules send_to(std::size_t device, AbsentAction if_absent)
        {
            PlacementRule rule;
            rule.name = "send";
            rule.match.placement_class = K_CLASS;
            rule.target.device_index = device;
            rule.if_absent = AbsentPolicy{if_absent, std::chrono::milliseconds{0}};
            PlacementRules rules;
            EXPECT_TRUE(rules.add(std::move(rule)));
            return rules;
        }
    } // namespace

    /** @brief Runs the RecorderPlacementScopeDevice tests on a host runtime. */
    class Small_RecorderPlacementScopeDevice : public RecorderHostTest
    {
    };

    /** @brief Runs the RecorderPlacementScopeDevice test that needs a second device. */
    class Medium_RecorderPlacementScopeDevice : public RecorderTest
    {
    };

    /** @brief Tests that On(scope) runs an untracked verb on device 0 when Local resolves it. */
    TEST_F(Small_RecorderPlacementScopeDevice, OnAScopeRunsAnUntrackedVerbOnTheLocalDevice)
    {
        const Runtime& rt = runtime();
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Local)));
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        auto data = rt.buffer<std::uint32_t>(K_COUNT).value();
        std::uint32_t* words = data.data();

        const std::size_t rule = recorder.untracked_kernel(
            K_COUNT, [words](std::size_t i) { words[i] = static_cast<std::uint32_t>(i) + 1u; },
            On(rt.placement_scope(K_CLASS)));

        EXPECT_EQ(recorder.refusal(), nullptr);
        EXPECT_EQ(rule, 0u);
        ASSERT_TRUE(graph.run());
        for (std::size_t i = 0; i < K_COUNT; ++i)
            ASSERT_EQ(words[i], i + 1u) << "element " << i;
    }

    /** @brief Tests that a scope with no matching rule records the verb without a refusal. */
    TEST_F(Small_RecorderPlacementScopeDevice, OnAScopeNoRuleMatchesRecordsTheVerb)
    {
        auto graph = runtime().graph();
        Recorder& recorder = graph.recorder();

        recorder.untracked_raw_kernel([](sycl::handler&) {}, On(runtime().placement_scope(K_CLASS)));

        EXPECT_EQ(recorder.refusal(), nullptr);
        EXPECT_EQ(recorder.rule_count(), 1u);
    }

    /** @brief Tests that a scope with no matching rule places its buffers on device 0. */
    TEST_F(Small_RecorderPlacementScopeDevice, AScopeNoRuleMatchesPlacesItsBufferOnDeviceZero)
    {
        const PlacementScope scope = runtime().placement_scope(K_CLASS);

        auto data = scope.buffer<std::uint32_t>(K_COUNT).value();

        EXPECT_EQ(data.device_index().value(), 0u);
    }

    /** @brief Tests that a Fail rule leaves a refusal naming the rule and the device, and records no node. */
    TEST_P(Small_RecorderPlacementScopeVerb, OnAScopeThatFailsRefusesTheVerb)
    {
        const Runtime& rt = runtime();
        const std::size_t absent = rt.advanced().device_count();
        ASSERT_TRUE(rt.set_placement_rules(send_to(absent, AbsentAction::Fail)));
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        const PlacementScope scope = rt.placement_scope(K_CLASS);

        if (GetParam() == Verb::CountKernel)
            recorder.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
        else
            recorder.untracked_raw_kernel([](sycl::handler&) {}, On(scope));

        ASSERT_NE(recorder.refusal(), nullptr);
        EXPECT_EQ(recorder.refusal()->code, ErrorCode::InvalidArgument);
        const std::string& message = recorder.refusal()->message;
        EXPECT_NE(message.find("\"send\""), std::string::npos) << message;
        EXPECT_NE(message.find("device index " + std::to_string(absent)), std::string::npos)
            << message;
        EXPECT_EQ(recorder.rule_count(), 0u) << "a refused verb records no node";
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_RecorderPlacementScopeVerb,
                             ::testing::Values(Verb::CountKernel, Verb::RawKernel));

    /** @brief Tests that a kept refusal ends the run with its error. */
    TEST_F(Small_RecorderPlacementScopeDevice, ARefusedScopeEndsTheRun)
    {
        const Runtime& rt = runtime();
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Fail)));
        auto graph = rt.graph();

        graph.recorder().untracked_kernel(K_COUNT, [](std::size_t) {},
                                          On(rt.placement_scope(K_CLASS)));

        const auto run = graph.run();
        ASSERT_FALSE(run);
        EXPECT_EQ(run.error().code, ErrorCode::InvalidArgument);
    }

    /** @brief Tests that a scope opened before a rule set is read against the new set at record time. */
    TEST_F(Small_RecorderPlacementScopeDevice, AScopeOpenedBeforeTheRulesFollowsTheNewSet)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        const PlacementScope scope = rt.placement_scope(K_CLASS);
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Fail)));

        recorder.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
        EXPECT_NE(recorder.refusal(), nullptr) << "the new set refuses";
    }

    /** @brief Tests that a cleared rule set leaves nothing to refuse. */
    TEST_F(Small_RecorderPlacementScopeDevice, AClearedSetLeavesNothingToRefuse)
    {
        const Runtime& rt = runtime();
        auto graph = rt.graph();
        Recorder& recorder = graph.recorder();
        const PlacementScope scope = rt.placement_scope(K_CLASS);
        ASSERT_TRUE(rt.set_placement_rules(
            send_to(rt.advanced().device_count(), AbsentAction::Fail)));
        ASSERT_TRUE(rt.clear_placement_rules());

        recorder.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
        EXPECT_EQ(recorder.refusal(), nullptr) << "the cleared set leaves nothing to refuse";
    }

    /** @brief Tests that a scope's buffer lands on device 1 and the tracked node that writes it runs there. */
    TEST_F(Medium_RecorderPlacementScopeDevice, AScopeBufferAndItsTrackedNodesLandOnDeviceOne)
    {
        const Runtime& rt = runtime();
        if (rt.advanced().device_count() < 2)
            GTEST_SKIP() << "needs a second device for the rule's target";
        ASSERT_TRUE(rt.set_placement_rules(send_to(1, AbsentAction::Fail)));
        const PlacementScope scope = rt.placement_scope(K_CLASS);
        auto data = scope.buffer<std::uint32_t>(K_COUNT).value();
        std::uint32_t* words = data.data();
        auto graph = rt.graph();

        graph.recorder().within(
            scope,
            [&](Recorder& in_scope)
            {
                in_scope.kernel(K_COUNT, Access(Reads(), Writes(data)),
                                [words](std::size_t i)
                                { words[i] = static_cast<std::uint32_t>(i) * 2u; });
                in_scope.untracked_kernel(K_COUNT, [](std::size_t) {}, On(scope));
            });

        EXPECT_EQ(data.device_index().value(), 1u);
        EXPECT_EQ(graph.recorder().refusal(), nullptr);
        ASSERT_TRUE(graph.run());
        for (std::size_t i = 0; i < K_COUNT; ++i)
            ASSERT_EQ(words[i], i * 2u) << "element " << i;
    }
} // namespace PlacementScopeDeviceRecordingTests

namespace PlacedTaskTests
{
    using namespace SushiRuntime;

    /** @brief Fixture of the RecorderPlacedTask tests, which run on the program's runtime. */
    class Medium_RecorderPlacedTask : public RecorderTest
    {
    };

    /** @brief Tests that a placed task over borrowed memory gets the queue of its device. */
    TEST_F(Medium_RecorderPlacedTask, ATaskOverBorrowedMemoryRunsOnTheDeviceItStates)
    {
        const Runtime& rt = runtime();
        const std::size_t device = rt.advanced().device_count() - 1;
        std::vector<std::uint32_t> memory(4, 0u);
        const LivenessToken owner = make_liveness();
        const Buffer<std::uint32_t> borrowed = rt.interop().borrow(
            memory.data(), memory.size(), DeviceIndex{device}, Residency::Host, owner).value();

        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(
            Access(Reads(), Writes(borrowed)),
            [&ran_on](sycl::queue& queue, const std::vector<sycl::event>&)
            {
                ran_on = queue.get_device();
                return sycl::event{};
            },
            On(DeviceIndex{device}));
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on,
                  rt.advanced().queue(DeviceIndex{device}).get_device());
    }

    /** @brief Fixture of the RecorderPlacedTaskDeathTest tests, which run on the program's runtime. */
    class Medium_RecorderPlacedTaskDeathTest : public RecorderTest
    {
    };

    /** @brief Tests that a task placed on a device the runtime lacks is refused at once. */
    TEST_F(Medium_RecorderPlacedTaskDeathTest, ATaskPlacedPastTheDeviceCountStops)
    {
        const Runtime& rt = runtime();
        const std::size_t count = rt.advanced().device_count();
        auto graph = rt.graph();
        EXPECT_DEATH(graph.recorder().task(
                         Access(),
                         [](sycl::queue&, const std::vector<sycl::event>&)
                         {
                             return sycl::event{};
                         },
                         On(DeviceIndex{count})),
                     "names no device");
    }

    /**
     * @brief Returns a device other than device 0 whose context is, or is not, device 0's.
     * @param shared Whether the device must share device 0's context.
     */
    std::optional<std::size_t> other_device(const Runtime& rt, bool shared)
    {
        const auto& devices = rt.advanced();
        for (std::size_t device = 1; device < devices.device_count(); ++device)
            if ((devices.queue(DeviceIndex{device}).get_context() ==
                 devices.queue(DeviceIndex{0}).get_context()) == shared)
                return device;
        return std::nullopt;
    }

    /** @brief Returns a body that notes the device of the queue it is handed. */
    auto noting_body(std::optional<sycl::device>& ran_on)
    {
        return [&ran_on](sycl::queue& queue, const std::vector<sycl::event>&)
        {
            ran_on = queue.get_device();
            return sycl::event{};
        };
    }

    /** @brief Tests that a stated device in the owner's context wins over the owner. */
    TEST_F(Medium_RecorderPlacedTask, AStatedDeviceInTheOwnersContextWinsOverTheOwner)
    {
        const Runtime& rt = runtime();
        const std::optional<std::size_t> device = other_device(rt, true);
        if (!device)
            GTEST_SKIP() << "no second device shares device 0's context";

        const Buffer<std::uint32_t> owned = rt.buffer<std::uint32_t>(4).value();
        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(Access(Reads(), Writes(owned)), noting_body(ran_on),
                              On(DeviceIndex{*device}));
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on,
                  rt.advanced().queue(DeviceIndex{*device}).get_device());
    }

    /** @brief Tests that a stated device in another context than the owner's is refused. */
    TEST_F(Medium_RecorderPlacedTask, AStatedDeviceInAnotherContextIsRefused)
    {
        const Runtime& rt = runtime();
        const std::optional<std::size_t> device = other_device(rt, false);
        if (!device)
            GTEST_SKIP() << "no device lies outside device 0's context";

        const Buffer<std::uint32_t> owned = rt.buffer<std::uint32_t>(4).value();
        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(Access(Reads(), Writes(owned)), noting_body(ran_on),
                              On(DeviceIndex{*device}));
        const auto refused = graph.run();

        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidGraph);
        EXPECT_NE(refused.error().message.find("another SYCL context"), std::string::npos)
            << refused.error().message;
        EXPECT_FALSE(ran_on.has_value()) << "a refused node is never moved to the owner";
    }

    /** @brief Tests that a task that states no device runs on the owner of its memory. */
    TEST_F(Medium_RecorderPlacedTask, ATaskThatStatesNoDeviceRunsOnTheOwner)
    {
        const Runtime& rt = runtime();
        const std::size_t device = rt.advanced().device_count() - 1;
        const Buffer<std::uint32_t> owned =
            rt.buffer<std::uint32_t>(4, DeviceIndex{device}).value();

        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        graph.recorder().task(Access(Reads(), Writes(owned)), noting_body(ran_on));
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on,
                  rt.advanced().queue(DeviceIndex{device}).get_device());
    }

    /** @brief Returns a rule set whose one rule sends class "recorder.ruled" to a device index. */
    PlacementRules rule_to_device(std::size_t device, AbsentAction if_absent)
    {
        PlacementRule rule;
        rule.name = "ruled";
        rule.match.placement_class = "recorder.ruled";
        rule.target.device_index = device;
        rule.if_absent = AbsentPolicy{if_absent, std::chrono::milliseconds{0}};
        PlacementRules rules;
        EXPECT_TRUE(rules.add(std::move(rule)));
        return rules;
    }

    /** @brief Records one task that states no device inside the scope the rule matches. */
    void record_ruled_task(const Runtime& rt, Graph& graph, std::optional<sycl::device>& ran_on)
    {
        graph.recorder().within(rt.placement_scope("recorder.ruled"),
                                [&ran_on](Recorder& in_scope)
                                { in_scope.task(Access(), noting_body(ran_on)); });
    }

    /** @brief Runs the RecorderRuledTask tests whose task body runs on a device. */
    class Medium_RecorderRuledTask : public RecorderTest
    {
    };

    /** @brief Runs the RecorderRuledTask test whose run is refused before any body runs, on a host runtime. */
    class Small_RecorderRuledTask : public RecorderHostTest
    {
    };

    /** @brief Tests that a rule naming device 1 of this machine runs its node on device 1. */
    TEST_F(Medium_RecorderRuledTask, Regression_ARuleNamingDeviceOneRunsTheNodeThere)
    {
        const Runtime& rt = runtime();
        if (rt.advanced().device_count() < 2)
            GTEST_SKIP() << "needs a second device for the rule's target";
        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(1, AbsentAction::Fail)));

        std::optional<sycl::device> ran_on;
        auto graph = rt.graph();
        record_ruled_task(rt, graph, ran_on);
        ASSERT_TRUE(graph.run());

        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on, rt.advanced().queue(DeviceIndex{1}).get_device());
    }

    /** @brief Tests that a rule naming a device this machine lacks runs its node on device 0 when it says Local. */
    TEST_F(Medium_RecorderRuledTask, Regression_ARuleNamingAnAbsentDeviceRunsLocalWhenItSaysLocal)
    {
        const Runtime& rt = runtime();
        const std::size_t absent = rt.advanced().device_count();

        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(absent, AbsentAction::Local)));
        std::optional<sycl::device> ran_on;
        auto local = rt.graph();
        record_ruled_task(rt, local, ran_on);
        ASSERT_TRUE(local.run());
        ASSERT_TRUE(ran_on.has_value());
        EXPECT_EQ(*ran_on, rt.advanced().queue(DeviceIndex{0}).get_device());
    }

    /** @brief Tests that a rule naming a device this machine lacks refuses its run when it says Fail. */
    TEST_F(Small_RecorderRuledTask, Regression_ARuleNamingAnAbsentDeviceRefusesWhenItSaysFail)
    {
        const Runtime& rt = runtime();
        const std::size_t absent = rt.advanced().device_count();

        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(absent, AbsentAction::Fail)));
        std::optional<sycl::device> ran_on;
        auto failing = rt.graph();
        record_ruled_task(rt, failing, ran_on);
        const auto refused = failing.run();
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("\"ruled\""), std::string::npos)
            << refused.error().message;
        EXPECT_FALSE(ran_on.has_value()) << "a refused compile runs nothing";
    }
} // namespace PlacedTaskTests
