/**************************************************************************/
/* test_runtime.cpp                                                       */
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
 * @file test_runtime.cpp
 * @brief Tests creating a Runtime, what it exposes and what it logs.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include <gtest/gtest.h>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_record.hpp>
#include <SushiLog/log/log_sink.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>

#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"
#include "support/runtime_contract.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

static_assert(!std::is_copy_constructible<Runtime>::value, "a Runtime must not copy");
static_assert(!std::is_copy_assignable<Runtime>::value, "a Runtime must not copy-assign");
static_assert(std::is_nothrow_move_constructible<Runtime>::value, "a Runtime must move");
static_assert(std::is_nothrow_move_assignable<Runtime>::value, "a Runtime must move-assign");

namespace
{
    /** @brief Returns the host name the operating system gives this machine, or an empty string. */
    std::string os_host_name()
    {
#if defined(_WIN32)
        DWORD size = 0;
        GetComputerNameExA(ComputerNameDnsHostname, nullptr, &size);
        std::string name(size, '\0');
        if (size == 0 || GetComputerNameExA(ComputerNameDnsHostname, name.data(), &size) == 0)
            return std::string();
        name.resize(size);
        return name;
#else
        char buffer[256] = {};
        if (gethostname(buffer, sizeof(buffer) - 1) != 0)
            return std::string();
        return std::string(buffer);
#endif
    }

    /** @brief Returns the operating system this test program was built for. */
    OperatingSystem built_for_operating_system()
    {
#if defined(_WIN32)
        return OperatingSystem::Windows;
#elif defined(__linux__)
        return OperatingSystem::Linux;
#else
        return OperatingSystem::Unknown;
#endif
    }

    /** @brief Lists the kind, name and memory of each device of @p facts, in order. */
    std::vector<std::tuple<DeviceKind, std::string, std::uint64_t>> device_rows(
        const std::vector<DeviceFacts>& devices)
    {
        std::vector<std::tuple<DeviceKind, std::string, std::uint64_t>> rows;
        for (const DeviceFacts& device : devices)
            rows.emplace_back(device.kind, device.name, device.memory_bytes);
        return rows;
    }

    /** @brief Lists the kind, name and memory of each device of @p devices, in order. */
    std::vector<std::tuple<DeviceKind, std::string, std::uint64_t>> device_rows(
        const std::vector<DeviceDescription>& devices)
    {
        std::vector<std::tuple<DeviceKind, std::string, std::uint64_t>> rows;
        for (const DeviceDescription& device : devices)
            rows.emplace_back(device.kind, device.name, device.memory_bytes);
        return rows;
    }

    /** @brief Lists the key and value of each label, in order. */
    std::vector<std::pair<std::string, std::string>> label_rows(const std::vector<NodeLabel>& labels)
    {
        std::vector<std::pair<std::string, std::string>> rows;
        for (const NodeLabel& label : labels)
            rows.emplace_back(label.key, label.value);
        return rows;
    }
} // namespace

/** @brief Fixture of the Runtime tests that run on a host runtime of their own. */
class Small_RuntimeEntry : public TestSupport::HostRuntimeTest
{
};

/** @brief Fixture of the Runtime tests that need a device, a real host name or the worker pool, on the program's runtime. */
class Medium_RuntimeEntry : public TestSupport::RuntimeTest
{
};

/** @brief Tests that the worker count of RuntimeConfig reaches the pool. */
TEST_F(Medium_RuntimeEntry, TheWorkerCountOfTheConfigReachesThePool)
{
    RuntimeConfig config;
    config.worker_count = 2;

    const Runtime rt = Runtime::create(config).value();

    EXPECT_EQ(rt.advanced().worker_pool().count, 2u);
}

/** @brief Tests that a view of a Runtime keeps answering after the Runtime is destroyed. */
TEST_F(Small_RuntimeEntry, TheViewOutlivesTheRuntime)
{
    std::optional<Runtime> rt(Runtime::create_host().value());
    const AdvancedControls view = rt->advanced();
    const std::size_t device_count = view.device_count();
    ASSERT_GE(device_count, 1u);

    rt.reset();

    EXPECT_EQ(view.device_count(), device_count);
    EXPECT_EQ(view.devices().size(), device_count);
}

/** @brief Tests that devices() has one entry per device. */
TEST_F(Small_RuntimeEntry, DevicesListsOneEntryPerDevice)
{
    const AdvancedControls view = runtime().advanced();

    ASSERT_FALSE(view.devices().empty());
    EXPECT_EQ(view.devices().size(), view.device_count());
}

/** @brief Fixture of the device order tests; the parameter is a device index, and a row past the machine's last device skips. */
class Medium_RuntimeDevices : public TestSupport::RuntimeTest,
                              public ::testing::WithParamInterface<std::size_t>
{
    protected:
        /** @brief Skips the row when the runtime drives no device at the parameter's index. */
        void SetUp() override
        {
            if (GetParam() >= runtime().advanced().device_count())
                GTEST_SKIP() << "the runtime drives no device " << GetParam();
        }
};

/** @brief Tests that entry i of devices() names the device behind the queue of DeviceIndex i. */
TEST_P(Medium_RuntimeDevices, EntryNamesTheDeviceBehindItsQueue)
{
    const AdvancedControls view = runtime().advanced();
    const std::string reported = view.queue(DeviceIndex{GetParam()}).get_device()
                                     .get_info<sycl::info::device::name>();

    EXPECT_EQ(view.devices()[GetParam()].index, GetParam());
    EXPECT_EQ(view.devices()[GetParam()].name, reported);
}

INSTANTIATE_TEST_SUITE_P(Devices, Medium_RuntimeDevices, ::testing::Range<std::size_t>(0, 4));

/** @brief Tests that a buffer with no placement lands on the first device of devices(). */
TEST_F(Small_RuntimeEntry, ABufferLandsOnTheFirstDevice)
{
    auto landed = runtime().buffer<int>(16).value();

    EXPECT_EQ(landed.device_index().value(), runtime().advanced().devices().front().index);
}

/** @brief Tests that topology() names the machine as the config does. */
TEST_F(Small_RuntimeEntry, TopologyNamesTheMachineAsTheConfigDoes)
{
    HostRuntimeConfig config;
    config.node_name = "forge-3";

    const Runtime rt = Runtime::create_host(config).value();

    EXPECT_EQ(rt.topology().name, "forge-3");
}

/** @brief Tests that topology() reports the operating system the program was built for. */
TEST_F(Small_RuntimeEntry, TopologyReportsTheOperatingSystem)
{
    EXPECT_EQ(runtime().topology().operating_system, built_for_operating_system());
}

/** @brief Tests that topology() lists the devices advanced() lists, in order. */
TEST_F(Small_RuntimeEntry, TopologyListsTheRuntimesDevices)
{
    EXPECT_EQ(device_rows(runtime().topology().devices), device_rows(runtime().advanced().devices()));
}

/** @brief Tests that topology() carries the labels the config gave, in order. */
TEST_F(Small_RuntimeEntry, TopologyCarriesTheConfiguredLabelsInOrder)
{
    HostRuntimeConfig config;
    config.labels = {NodeLabel{"zone", "north"}, NodeLabel{"gpu", ""}, NodeLabel{"tier", "a"}};

    const Runtime rt = Runtime::create_host(config).value();

    EXPECT_EQ(label_rows(rt.topology().labels), label_rows(config.labels));
}

/** @brief Tests that a config with no node_name makes topology() report the host name the OS gives. */
TEST_F(Medium_RuntimeEntry, TopologyNamesTheMachineByItsHostNameWhenTheConfigGivesNone)
{
    EXPECT_EQ(runtime().topology().name, os_host_name());
}

/** @brief Tests that a config with no labels makes topology() report none. */
TEST_F(Small_RuntimeEntry, TopologyCarriesNoLabelWhenTheConfigGivesNone)
{
    EXPECT_TRUE(runtime().topology().labels.empty());
}

namespace
{
    /** @brief Returns a rule that sends the matched class to device 0 at the given priority. */
    PlacementRule rule_for(const char* pattern, std::optional<std::uint64_t> key, int priority)
    {
        PlacementRule rule;
        rule.match.placement_class = pattern;
        rule.match.key = key;
        rule.target.device_index = 0;
        rule.priority = priority;
        return rule;
    }

    /** @brief Returns a rule set of one rule for "sushiai.replica", at priority 1. */
    PlacementRules replica_rules()
    {
        PlacementRules rules;
        EXPECT_TRUE(rules.add(rule_for("sushiai.replica", std::nullopt, 1)));
        return rules;
    }

    /** @brief Returns the replica rule plus a rule for the class prefix and key 0 at priority 5, which wins for a keyed replica. */
    PlacementRules replica_and_prefix_rules()
    {
        PlacementRules rules = replica_rules();
        EXPECT_TRUE(rules.add(rule_for("sushiai.*", 0u, 5)));
        return rules;
    }

    /** @brief Returns a rule set that refuses the class "sushiai.replica": its target is a device no machine has. */
    PlacementRules refusing_rules(const Runtime& runtime)
    {
        PlacementRule rule;
        rule.match.placement_class = "sushiai.replica";
        rule.target.device_index = runtime.advanced().device_count();
        rule.if_absent = AbsentPolicy{AbsentAction::Fail, std::chrono::milliseconds{0}};
        PlacementRules rules;
        EXPECT_TRUE(rules.add(std::move(rule)));
        return rules;
    }

    /** @brief Records a replica node and a node with no class, each writing its own buffer, into @p graph. */
    void record_replica_and_plain(Graph& graph, const Buffer<int>& first, const Buffer<int>& second)
    {
        int* a = first.data();
        int* b = second.data();
        NodeOptions replica;
        replica.placement_class = "sushiai.replica"_class;
        replica.placement_key = 0;
        graph.add(4, Reads(), Writes(first), [a](std::size_t i) { a[i] = 1; }, replica);
        graph.add(4, Reads(), Writes(second), [b](std::size_t i) { b[i] = 2; });
    }
} // namespace

/** @brief Fixture of the placement rule tests on a host runtime of their own, which starts with no rules in force. */
class Small_RuntimePlacementRules : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that a graph compiled with no rule in force reports no winner for any node. */
TEST_F(Small_RuntimePlacementRules, ARunWithNoRulesReportsNoWinner)
{
    auto first = runtime().buffer<int>(4).value();
    auto second = runtime().buffer<int>(4).value();
    auto graph = runtime().graph();
    record_replica_and_plain(graph, first, second);

    ASSERT_TRUE(graph.run());

    const PlanView plan = graph.plan().value();
    ASSERT_EQ(plan.nodes.size(), 2u);
    EXPECT_FALSE(plan.nodes[0].placement_rule.has_value());
    EXPECT_FALSE(plan.nodes[1].placement_rule.has_value());
}

/** @brief Tests that a node with no class matches no rule that states one. */
TEST_F(Small_RuntimePlacementRules, ANodeWithNoClassMatchesNoRuleThatStatesOne)
{
    auto first = runtime().buffer<int>(4).value();
    auto second = runtime().buffer<int>(4).value();
    auto graph = runtime().graph();
    record_replica_and_plain(graph, first, second);
    ASSERT_TRUE(runtime().set_placement_rules(replica_rules()));

    ASSERT_TRUE(graph.run());

    const PlanView plan = graph.plan().value();
    EXPECT_EQ(plan.nodes[0].placement_rule, std::optional<std::size_t>(0u));
    EXPECT_FALSE(plan.nodes[1].placement_rule.has_value());
}

/** @brief Tests that a new rule set makes the plan stale, so the next run compiles again. */
TEST_F(Small_RuntimePlacementRules, ANewRuleSetRecompilesThePlan)
{
    auto first = runtime().buffer<int>(4).value();
    auto second = runtime().buffer<int>(4).value();
    auto graph = runtime().graph();
    record_replica_and_plain(graph, first, second);
    ASSERT_TRUE(graph.run());
    ASSERT_TRUE(runtime().set_placement_rules(replica_rules()));

    ASSERT_TRUE(graph.run());

    EXPECT_EQ(graph.compile_count(), 2u);
}

/** @brief Tests that a second compile reports the winner of the new rule set. */
TEST_F(Small_RuntimePlacementRules, ASecondCompileReportsTheWinnerOfTheNewRules)
{
    auto first = runtime().buffer<int>(4).value();
    auto second = runtime().buffer<int>(4).value();
    auto graph = runtime().graph();
    record_replica_and_plain(graph, first, second);
    ASSERT_TRUE(runtime().set_placement_rules(replica_rules()));
    ASSERT_TRUE(graph.run());
    ASSERT_TRUE(runtime().set_placement_rules(replica_and_prefix_rules()));

    ASSERT_TRUE(graph.run());

    EXPECT_EQ(graph.plan().value().nodes[0].placement_rule, std::optional<std::size_t>(1u));
}

/** @brief Tests that an unchanged rule set does not rebuild the plan. */
TEST_F(Small_RuntimePlacementRules, AnUnchangedRuleSetDoesNotRebuildThePlan)
{
    auto first = runtime().buffer<int>(4).value();
    auto second = runtime().buffer<int>(4).value();
    auto graph = runtime().graph();
    record_replica_and_plain(graph, first, second);
    ASSERT_TRUE(runtime().set_placement_rules(replica_rules()));
    ASSERT_TRUE(graph.run());
    const std::size_t compiled = graph.compile_count();

    ASSERT_TRUE(graph.run());

    EXPECT_EQ(graph.compile_count(), compiled);
}

/** @brief Tests that clearing the rules makes the next compile report no winner. */
TEST_F(Small_RuntimePlacementRules, ClearingTheRulesReportsNoWinner)
{
    auto first = runtime().buffer<int>(4).value();
    auto second = runtime().buffer<int>(4).value();
    auto graph = runtime().graph();
    record_replica_and_plain(graph, first, second);
    ASSERT_TRUE(runtime().set_placement_rules(replica_rules()));
    ASSERT_TRUE(graph.run());
    ASSERT_TRUE(runtime().clear_placement_rules());

    ASSERT_TRUE(graph.run());

    EXPECT_FALSE(graph.plan().value().nodes[0].placement_rule.has_value());
}

namespace
{
    /** @brief Holds a run in flight whose host task waits in the executor's queue, so a test can call the runtime meanwhile. */
    class PendingRun
    {
        public:
            /** @brief Starts a run of a graph whose only task stays queued until finish() drains it. */
            explicit PendingRun(const Runtime& runtime)
                : target_(runtime.buffer<int>(4).value()), graph_(runtime.graph())
            {
                int* data = target_.data();
                graph_.add_host(Reads{}, Writes(target_), [data]() { data[0] = 7; });
                step_.emplace(graph_.run_async().value());
            }

            /** @brief Runs the queued task and waits for the run to end. */
            void finish() { step_->wait().value(); }

        private:
            /** @brief The buffer the task writes. */
            Buffer<int> target_;

            /** @brief The graph in flight. */
            Graph graph_;

            /** @brief The run in flight. */
            std::optional<RunHandle<Graph>> step_;
    };
} // namespace

/** @brief Tests that set_placement_rules() is refused as InvalidState while a run is in flight. */
TEST_F(Small_RuntimePlacementRules, SettingTheRulesDuringARunIsRefusedAsInvalidState)
{
    PendingRun run(runtime());

    const Result<void> set = runtime().set_placement_rules(replica_rules());
    run.finish();

    ASSERT_FALSE(set.has_value());
    EXPECT_EQ(set.error().code, ErrorCode::InvalidState);
    EXPECT_NE(set.error().message.find("wait"), std::string::npos) << set.error().message;
}

/** @brief Tests that clear_placement_rules() is refused as InvalidState while a run is in flight. */
TEST_F(Small_RuntimePlacementRules, ClearingTheRulesDuringARunIsRefusedAsInvalidState)
{
    PendingRun run(runtime());

    const Result<void> cleared = runtime().clear_placement_rules();
    run.finish();

    ASSERT_FALSE(cleared.has_value());
    EXPECT_EQ(cleared.error().code, ErrorCode::InvalidState);
}

/** @brief Tests that set and clear both succeed once the run ended. */
TEST_F(Small_RuntimePlacementRules, TheRulesCanBeChangedOnceTheRunEnded)
{
    PendingRun run(runtime());
    run.finish();

    const Result<void> set = runtime().set_placement_rules(replica_rules());
    const Result<void> cleared = runtime().clear_placement_rules();

    EXPECT_TRUE(set);
    EXPECT_TRUE(cleared);
}

namespace
{
    /** @brief Applies rule changes to a runtime from its own thread until it is stopped. */
    class RuleWriter
    {
        public:
            /** @brief Starts the thread; it alternates between the refusing set and a cleared set. */
            explicit RuleWriter(const Runtime& runtime) : thread_([this, &runtime]() { alternate(runtime); }) {}

            RuleWriter(const RuleWriter&) = delete;
            RuleWriter& operator=(const RuleWriter&) = delete;

            /** @brief Stops the thread. */
            ~RuleWriter() { stop(); }

            /** @brief Stops the thread and waits for it. */
            void stop()
            {
                stop_.store(true, std::memory_order_release);
                if (thread_.joinable())
                    thread_.join();
            }

            /** @brief Returns how many changes the runtime accepted. */
            [[nodiscard]] std::uint64_t applied() const { return applied_.load(std::memory_order_relaxed); }

        private:
            /** @brief Alternates the set and the clear until stopped; a refusal must be InvalidState. */
            void alternate(const Runtime& runtime)
            {
                for (int round = 0; !stop_.load(std::memory_order_acquire); ++round)
                {
                    const Result<void> done = (round % 2 == 0)
                        ? runtime.set_placement_rules(refusing_rules(runtime))
                        : runtime.clear_placement_rules();
                    if (done)
                        applied_.fetch_add(1, std::memory_order_relaxed);
                    else
                        EXPECT_EQ(done.error().code, ErrorCode::InvalidState);
                }
            }

            /** @brief Set to end the thread. */
            std::atomic<bool> stop_{false};

            /** @brief Changes the runtime accepted. */
            std::atomic<std::uint64_t> applied_{0};

            /** @brief The thread; declared last, so every other member exists before it runs. */
            std::thread thread_;
    };

    /** @brief Records into @p graph a host task that asks @p scope for its device twice, a yield apart, and counts the runs where the answers differ. */
    void record_scope_probe(Graph& graph, const Buffer<int>& target, const PlacementScope& scope,
                            std::atomic<std::uint64_t>& differing)
    {
        graph.add_host(Reads{}, Writes(target), [scope, &differing]()
        {
            const bool at_start = scope.device().has_value();
            std::this_thread::yield();
            if (scope.device().has_value() != at_start)
                differing.fetch_add(1, std::memory_order_relaxed);
        });
    }

    /** @brief Runs @p graph @p runs times and returns how many runs succeeded. */
    std::size_t successful_runs(Graph& graph, std::size_t runs)
    {
        std::size_t succeeded = 0;
        for (std::size_t run = 0; run < runs; ++run)
            if (graph.run())
                ++succeeded;
        return succeeded;
    }

    /** @brief Reads the scope's device in a loop until stopped, as a second thread of readers does. */
    class ScopeReader
    {
        public:
            /** @brief Starts the thread. */
            explicit ScopeReader(const PlacementScope& scope)
                : thread_([this, scope]() { read(scope); })
            {
            }

            ScopeReader(const ScopeReader&) = delete;
            ScopeReader& operator=(const ScopeReader&) = delete;

            /** @brief Stops the thread. */
            ~ScopeReader() { stop(); }

            /** @brief Stops the thread and waits for it. */
            void stop()
            {
                stop_.store(true, std::memory_order_release);
                if (thread_.joinable())
                    thread_.join();
            }

        private:
            /** @brief Asks the scope for its device until stopped. */
            void read(const PlacementScope& scope)
            {
                while (!stop_.load(std::memory_order_acquire))
                    static_cast<void>(scope.device());
            }

            /** @brief Set to end the thread. */
            std::atomic<bool> stop_{false};

            /** @brief The thread; declared last. */
            std::thread thread_;
    };
} // namespace

/** @brief Fixture of the placement rule race test on the program's runtime, which starts and ends with no rules in force. */
class Medium_RuntimePlacementRules : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Clears the rules an earlier test may have left. */
        void SetUp() override { static_cast<void>(runtime().clear_placement_rules()); }

        /** @brief Clears the rules this test set. */
        void TearDown() override { static_cast<void>(runtime().clear_placement_rules()); }
};

/** @brief Tests that a rule change racing run starts and scope reads never lands inside a run. */
TEST_F(Medium_RuntimePlacementRules, Regression_ARuleChangeRacingARunStartNeverLandsInsideTheRun)
{
    constexpr std::size_t K_RUNS = 2000;
    const PlacementScope scope = runtime().placement_scope("sushiai.replica");
    auto target = runtime().buffer<int>(1).value();
    std::atomic<std::uint64_t> differing{0};
    auto graph = runtime().graph();
    record_scope_probe(graph, target, scope, differing);
    RuleWriter writer(runtime());
    ScopeReader first_reader(scope);
    ScopeReader second_reader(scope);

    const std::size_t succeeded = successful_runs(graph, K_RUNS);
    writer.stop();
    first_reader.stop();
    second_reader.stop();

    EXPECT_EQ(succeeded, K_RUNS);
    EXPECT_EQ(differing.load(), 0u);
    EXPECT_GT(writer.applied(), 0u) << "the writer never found the runtime idle";
}

/** @brief Holds one label set create() refuses and the text its message must carry. */
struct RefusedLabels
{
    const char* name;               /**< Names the row. */
    std::vector<NodeLabel> labels;  /**< The labels the config carries. */
    std::string named;              /**< What the message must name. */
};

/** @brief Fixture of the label tests; the parameter is the row. */
class Small_RuntimeLabels : public ::testing::TestWithParam<RefusedLabels>
{
};

/** @brief Tests that create() refuses a label set that breaks a rule and names what is wrong. */
TEST_P(Small_RuntimeLabels, CreateRefusesALabelKeyThatBreaksARule)
{
    HostRuntimeConfig config;
    config.labels = GetParam().labels;

    const auto refused = Runtime::create_host(config);

    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(refused.error().message.find(GetParam().named), std::string::npos)
        << refused.error().message;
    EXPECT_NE(refused.error().message.find("RuntimeConfig::labels"), std::string::npos)
        << refused.error().message;
}

INSTANTIATE_TEST_SUITE_P(
    Keys, Small_RuntimeLabels,
    ::testing::Values(
        RefusedLabels{"AnEmptyKey", {NodeLabel{"zone", "a"}, NodeLabel{"", "b"}}, "position 1"},
        RefusedLabels{"AKeyHoldingEquals", {NodeLabel{"a=b", "c"}}, "\"a=b\""},
        RefusedLabels{"ARepeatedKey",
                      {NodeLabel{"zone", "a"}, NodeLabel{"rack", "b"}, NodeLabel{"zone", "c"}},
                      "\"zone\""}),
    [](const ::testing::TestParamInfo<RefusedLabels>& info) { return std::string(info.param.name); });

/** @brief Tests that the pinning policy of RuntimeConfig reaches the pool. */
TEST_F(Medium_RuntimeEntry, ThePinningPolicyOfTheConfigReachesThePool)
{
    RuntimeConfig config;
    config.worker_count = 1;
    config.pinning = PinPolicy::Float;

    const Runtime rt = Runtime::create(config).value();

    EXPECT_EQ(rt.advanced().worker_pool().pinning, PinPolicy::Float);
}

/** @brief Tests that a view taken before a Runtime moves answers for the moved Runtime. */
TEST_F(Small_RuntimeEntry, AMovedRuntimeKeepsTheContextItsViewsShare)
{
    Runtime first = Runtime::create_host().value();
    const AdvancedControls view = first.advanced();
    const std::size_t before = view.live_allocation_count();

    const Runtime second = std::move(first);
    const auto buffer = second.buffer<int>(8).value();

    EXPECT_EQ(view.live_allocation_count(), before + 1);
}

/**
 * @brief Tests that a handle a Runtime hands out reaches the Runtime's own context: an allocation
 *        made on the Runtime is read back through the handle, and so is its release.
 */
TEST_F(Small_RuntimeEntry, AHandleItHandsOutReachesItsOwnContext)
{
    const AdvancedControls controls = runtime().advanced();
    const std::size_t before = controls.live_allocation_count();
    std::optional<Buffer<int>> held(runtime().buffer<int>(8).value());
    EXPECT_EQ(controls.live_allocation_count(), before + 1);

    held.reset();

    EXPECT_EQ(controls.live_allocation_count(), before);
}

/** @brief Tests that a caller waits for device work and copies through the device's queue. */
TEST_F(Medium_RuntimeEntry, ACallerWaitsAndCopiesThroughTheDeviceQueue)
{
    constexpr std::size_t K_WORDS = 256;
    const Buffer<std::uint32_t> values = runtime().buffer<std::uint32_t>(K_WORDS).value();
    std::uint32_t* words = values.data();
    sycl::queue& queue = runtime().advanced().queue(values.device_index());
    queue.parallel_for(sycl::range<1>(K_WORDS), [words](sycl::id<1> i)
    {
        words[i] = static_cast<std::uint32_t>(i[0]) + 1u;
    });
    runtime().wait().value();
    std::vector<std::uint32_t> copied(K_WORDS, 0u);

    queue.memcpy(copied.data(), words, K_WORDS * sizeof(std::uint32_t)).wait();

    EXPECT_EQ(copied, TestSupport::ramp<std::uint32_t>(K_WORDS, 1u));
}

/** @brief Fixture of the death tests of Runtime, which run on the program's runtime. */
class Medium_RuntimeEntryDeathTest : public TestSupport::RuntimeTest
{
};

/** @brief Tests that asking for the queue of a device the runtime does not have stops. */
TEST_F(Medium_RuntimeEntryDeathTest, AQueueOfAnUnknownDeviceStops)
{
    const AdvancedControls controls = runtime().advanced();

    EXPECT_DEATH(static_cast<void>(controls.queue(DeviceIndex{controls.device_count()})),
                 "Device index .* is out of bounds");
}

/** @brief Tests that a buffer no device can hold comes back as AllocationFailed with its count in the message. */
TEST_F(Medium_RuntimeEntry, ABufferNoDeviceCanHoldReturnsAllocationFailed)
{
    // Half the address space in bytes: the count fits std::size_t and no device has the room.
    const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

    const auto buffer = runtime().buffer<std::uint8_t>(count);

    ASSERT_FALSE(buffer.has_value());
    EXPECT_EQ(buffer.error().code, ErrorCode::AllocationFailed);
    EXPECT_NE(buffer.error().message.find(std::to_string(count) + " elements"), std::string::npos)
        << buffer.error().message;
}

/** @brief Tests that a state no device can hold comes back as AllocationFailed. */
TEST_F(Small_RuntimeEntry, AStateNoDeviceCanHoldReturnsAllocationFailed)
{
    const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

    const auto state = runtime().state<std::uint8_t>(count);

    ASSERT_FALSE(state.has_value());
    EXPECT_EQ(state.error().code, ErrorCode::AllocationFailed);
}

/** @brief Tests that a failed allocation leaves the runtime able to allocate again. */
TEST_F(Small_RuntimeEntry, AFailedAllocationLeavesTheRuntimeUsable)
{
    static_cast<void>(runtime().buffer<std::uint8_t>(std::numeric_limits<std::size_t>::max() / 2));

    const auto small = runtime().buffer<std::uint8_t>(16);

    EXPECT_TRUE(small.has_value());
}

/** @brief Tests that a buffer adds one live allocation. */
TEST_F(Small_RuntimeEntry, ABufferAddsOneLiveAllocation)
{
    const std::size_t before = runtime().advanced().live_allocation_count();

    const auto buf = runtime().buffer<int>(8).value();

    EXPECT_EQ(runtime().advanced().live_allocation_count(), before + 1);
}

/** @brief Tests that a state adds two live allocations, one per field. */
TEST_F(Small_RuntimeEntry, AStateAddsTwoLiveAllocations)
{
    const std::size_t before = runtime().advanced().live_allocation_count();

    const auto field = runtime().state<int>(8).value();

    EXPECT_EQ(runtime().advanced().live_allocation_count(), before + 2);
}

/** @brief Tests that freeing a buffer and a state returns the live allocation count to what it was. */
TEST_F(Small_RuntimeEntry, FreeingABufferAndAStateReturnsTheLiveAllocationCount)
{
    const std::size_t before = runtime().advanced().live_allocation_count();
    {
        const auto buf = runtime().buffer<int>(8).value();
        const auto field = runtime().state<int>(8).value();
    }

    EXPECT_EQ(runtime().advanced().live_allocation_count(), before);
}

/** @brief Tests that a thread that outlives the Runtime it allocated from tears down without a fault. */
TEST(Medium_Runtime, AThreadOutlivingTheRuntimeTearsDownCleanly)
{
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool allocated = false;
    bool runtime_gone = false;
    std::atomic<bool> unwound{false};

    std::thread outliver;

    {
        auto rt = SushiRuntime::Runtime::create().value();

        outliver = std::thread([&]()
        {
            // The thread's exit-time destructors touch per-thread runtime state; see README.md.
            {
                auto scratch = rt.buffer<int>(4096).value();
                TestSupport::fill_with(scratch, 7);
            }

            {
                std::lock_guard<std::mutex> lock(gate_mutex);
                allocated = true;
            }
            gate.notify_one();

            // Stay alive until the Runtime and its allocator are gone.
            std::unique_lock<std::mutex> lock(gate_mutex);
            gate.wait(lock, [&] { return runtime_gone; });
            unwound.store(true);
        });

        std::unique_lock<std::mutex> lock(gate_mutex);
        gate.wait(lock, [&] { return allocated; });
    }   // Runtime destroyed here, while the other thread is still alive.

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        runtime_gone = true;
    }
    gate.notify_one();
    outliver.join();

    EXPECT_TRUE(unwound.load()) << "the thread did not unwind after the Runtime it allocated from was gone";
}

namespace
{
    /** @brief Holds one record a test sink received: its level and a copy of its line. */
    using SeenRecord = std::pair<SushiLog::LogLevel, std::string>;

    /** @brief Creates and destroys a default Runtime. */
    void create_and_destroy_a_runtime()
    {
        auto runtime = Runtime::create().value();
        EXPECT_GE(runtime.advanced().device_count(), 1u);
    }

    /** @brief Tells whether @p records hold an Info line that names a device. */
    bool has_info_device_line(const std::vector<SeenRecord>& records)
    {
        return std::any_of(records.begin(), records.end(), [](const SeenRecord& record)
        {
            return record.first == SushiLog::LogLevel::Info &&
                   record.second.find("Device: ") != std::string::npos;
        });
    }
} // namespace

/** @brief Base of the logging tests: the logger is synchronous and sends its records to a vector, and the level and the sink are put back. */
class RecordedLogTest : public ::testing::Test
{
    protected:
        /** @brief Puts the logger in synchronous mode and installs a sink that keeps what it receives. */
        void SetUp() override
        {
            saved_level_ = SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT);
            SushiLog::set_log_synchronous(true);
            SushiLog::set_log_sink([this](const SushiLog::LogRecord& record)
            {
                records_.emplace_back(record.level, std::string(record.text));
            });
        }

        /** @brief Puts the default sink and the saved level back. */
        void TearDown() override
        {
            SushiLog::set_log_sink(nullptr);
            SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, saved_level_);
        }

        /** @brief The records the sink received. */
        std::vector<SeenRecord> records_;

    private:
        /// @brief The level the process had when the test began.
        SushiLog::LogLevel saved_level_ = SushiLog::LogLevel::Warn;
};

/** @brief Fixture of the logging test that creates a host runtime, which opens no device. */
class Small_LoggingDefaults : public RecordedLogTest
{
};

/** @brief Fixture of the logging test that creates a runtime on the machine's devices. */
class Medium_LoggingDefaults : public RecordedLogTest
{
};

/** @brief Tests that creating a Runtime sends nothing to the sink while the level is Warn. */
TEST_F(Small_LoggingDefaults, CreateIsSilentAtTheDefaultLevel)
{
    SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Warn);

    static_cast<void>(Runtime::create_host().value());

    EXPECT_TRUE(records_.empty()) << records_.size() << " records reached the sink at Warn";
}

/** @brief Tests that at Info creating a Runtime sends an Info line that names a device. */
TEST_F(Medium_LoggingDefaults, DeviceLineArrivesThroughTheSinkAtInfo)
{
    SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Info);

    create_and_destroy_a_runtime();

    EXPECT_TRUE(has_info_device_line(records_)) << records_.size() << " records, none an Info device line";
}

namespace
{
    /** @brief Makes the runtime the contract runs on: the program's device runtime. */
    struct DeviceRuntimeMaker
    {
        /** @brief Returns the runtime every medium test of the program shares. */
        static SushiRuntime::Runtime& runtime(std::optional<SushiRuntime::Runtime>& /*slot*/)
        {
            return TestSupport::RuntimeEnvironment::runtime();
        }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the runtime contract on a device, the boundary the host runtime stands in for. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_DeviceRuntime, RuntimeContract, ::DeviceRuntimeMaker, );
} // namespace TestSupport
