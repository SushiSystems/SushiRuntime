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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_runtime.cpp
 * @brief Tests creating a Runtime, what it exposes and what it logs.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_record.hpp>
#include <SushiLog/log/log_sink.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

using namespace SushiRuntime;

static_assert(!std::is_copy_constructible<Runtime>::value, "a Runtime must not copy");
static_assert(!std::is_copy_assignable<Runtime>::value, "a Runtime must not copy-assign");
static_assert(std::is_nothrow_move_constructible<Runtime>::value, "a Runtime must move");
static_assert(std::is_nothrow_move_assignable<Runtime>::value, "a Runtime must move-assign");

/** @brief Tests that one struct sets the worker count and that a view outlives its Runtime. */
TEST(Medium_RuntimeEntry, OneStructConfiguresAndTheViewOutlivesTheRuntime)
{
    RuntimeConfig config;
    config.worker_count = 2;

    std::optional<Runtime> rt(Runtime::create(config).value());
    const AdvancedControls view = rt->advanced();

    EXPECT_EQ(view.worker_pool().count, 2u);
    const std::size_t device_count = view.device_count();
    ASSERT_GE(device_count, 1u);

    rt.reset();

    EXPECT_EQ(view.device_count(), device_count);
    EXPECT_EQ(view.devices().size(), device_count);
    EXPECT_EQ(view.worker_pool().count, 2u);
}

/** @brief Tests that devices() has one entry per device, in the order DeviceIndex counts. */
TEST(Medium_RuntimeEntry, DevicesListsEveryDeviceInOrder)
{
    const Runtime rt = Runtime::create().value();
    const AdvancedControls view = rt.advanced();
    const std::vector<DeviceDescription>& devices = view.devices();

    ASSERT_EQ(devices.size(), view.device_count());
    ASSERT_FALSE(devices.empty());

    Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(rt);
    for (std::size_t index = 0; index < devices.size(); ++index)
    {
        const std::string reported = context.devices().queue(index).get_device()
                                         .get_info<sycl::info::device::name>();
        EXPECT_EQ(devices[index].index, index);
        EXPECT_EQ(devices[index].name, reported) << "entry " << index;
    }

    auto landed = rt.buffer<int>(16).value();
    const std::optional<std::size_t> owner = context.memory().device_of(landed.data());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, devices.front().index);
    EXPECT_EQ(landed.device_index().value(), devices.front().index);
}

/** @brief Tests that topology() reports the machine, its devices and the labels the config gave. */
TEST(Medium_RuntimeEntry, TopologyReportsTheLocalMachine)
{
    RuntimeConfig config;
    config.node_name = "forge-3";
    config.labels = {NodeLabel{"zone", "north"}, NodeLabel{"gpu", ""}, NodeLabel{"tier", "a"}};

    const Runtime rt = Runtime::create(config).value();
    const NodeFacts& facts = rt.topology();
    const std::vector<DeviceDescription>& devices = rt.advanced().devices();

    EXPECT_EQ(facts.name, "forge-3");
#if defined(_WIN32)
    EXPECT_EQ(facts.operating_system, OperatingSystem::Windows);
#elif defined(__linux__)
    EXPECT_EQ(facts.operating_system, OperatingSystem::Linux);
#else
    EXPECT_EQ(facts.operating_system, OperatingSystem::Unknown);
#endif

    ASSERT_EQ(facts.devices.size(), rt.advanced().device_count());
    ASSERT_EQ(facts.devices.size(), devices.size());
    for (std::size_t index = 0; index < devices.size(); ++index)
    {
        EXPECT_EQ(facts.devices[index].kind, devices[index].kind) << "device " << index;
        EXPECT_EQ(facts.devices[index].name, devices[index].name) << "device " << index;
        EXPECT_EQ(facts.devices[index].memory_bytes, devices[index].memory_bytes)
            << "device " << index;
    }

    ASSERT_EQ(facts.labels.size(), config.labels.size());
    for (std::size_t index = 0; index < facts.labels.size(); ++index)
    {
        EXPECT_EQ(facts.labels[index].key, config.labels[index].key) << "label " << index;
        EXPECT_EQ(facts.labels[index].value, config.labels[index].value) << "label " << index;
    }
}

/** @brief Tests that an empty node_name makes topology() report the host name. */
TEST(Medium_RuntimeEntry, TopologyNamesTheMachineByItsHostNameWhenTheConfigGivesNone)
{
    const Runtime rt = Runtime::create().value();

    EXPECT_FALSE(rt.topology().name.empty());
    EXPECT_TRUE(rt.topology().labels.empty());
}

namespace
{
    constexpr auto K_RULES_GATE_TIMEOUT = std::chrono::milliseconds(2000);

    /** @brief Blocks until the flag is set or the deadline passes. */
    void wait_for_rules_gate(const std::atomic<bool>& released)
    {
        const auto deadline = std::chrono::steady_clock::now() + K_RULES_GATE_TIMEOUT;
        while (!released.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    }

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
} // namespace

/** @brief Tests that new rules between two runs change the winner the next plan reports. */
TEST(Medium_RuntimePlacementRules, ASecondCompileReportsTheWinnerOfTheNewRules)
{
    const Runtime rt = Runtime::create().value();
    auto first = rt.buffer<int>(4).value();
    auto second = rt.buffer<int>(4).value();
    int* a = first.data();
    int* b = second.data();

    NodeOptions replica;
    replica.placement_class = "sushiai.replica"_class;
    replica.placement_key = 0;

    auto graph = rt.graph();
    graph.add(4, Reads(), Writes(first), [a](std::size_t i) { a[i] = 1; }, replica);
    graph.add(4, Reads(), Writes(second), [b](std::size_t i) { b[i] = 2; });

    ASSERT_TRUE(graph.run());
    const PlanView without_rules = graph.plan().value();
    ASSERT_EQ(without_rules.nodes.size(), 2u);
    EXPECT_FALSE(without_rules.nodes[0].placement_rule.has_value()) << "no rule set is in force";
    EXPECT_FALSE(without_rules.nodes[1].placement_rule.has_value());

    PlacementRules first_rules;
    ASSERT_TRUE(first_rules.add(rule_for("sushiai.replica", std::nullopt, 1)));
    ASSERT_TRUE(rt.set_placement_rules(std::move(first_rules)));
    ASSERT_TRUE(graph.run());
    EXPECT_EQ(graph.compile_count(), 2u) << "a new rule set makes the plan stale";
    const PlanView under_first = graph.plan().value();
    ASSERT_TRUE(under_first.nodes[0].placement_rule.has_value());
    EXPECT_EQ(*under_first.nodes[0].placement_rule, 0u);
    EXPECT_FALSE(under_first.nodes[1].placement_rule.has_value())
        << "a node with no class matches no rule that states one";

    PlacementRules second_rules;
    ASSERT_TRUE(second_rules.add(rule_for("sushiai.replica", std::nullopt, 1)));
    ASSERT_TRUE(second_rules.add(rule_for("sushiai.*", 0u, 5)));
    ASSERT_TRUE(rt.set_placement_rules(std::move(second_rules)));
    ASSERT_TRUE(graph.run());
    EXPECT_EQ(graph.compile_count(), 3u);
    const PlanView under_second = graph.plan().value();
    ASSERT_TRUE(under_second.nodes[0].placement_rule.has_value());
    EXPECT_EQ(*under_second.nodes[0].placement_rule, 1u);
    EXPECT_FALSE(under_second.nodes[1].placement_rule.has_value());

    ASSERT_TRUE(graph.run());
    EXPECT_EQ(graph.compile_count(), 3u) << "an unchanged rule set does not rebuild the plan";

    ASSERT_TRUE(rt.clear_placement_rules());
    ASSERT_TRUE(graph.run());
    EXPECT_EQ(graph.compile_count(), 4u);
    EXPECT_FALSE(graph.plan().value().nodes[0].placement_rule.has_value());
    EXPECT_EQ(a[0], 1);
    EXPECT_EQ(b[0], 2);
}

/** @brief Tests that the rules cannot be changed while a run is in flight. */
TEST(Medium_RuntimePlacementRules, ChangingTheRulesDuringARunIsRefusedAsInvalidState)
{
    const Runtime rt = Runtime::create().value();
    auto target = rt.buffer<int>(4).value();
    int* data = target.data();
    std::atomic<bool> released{false};

    auto graph = rt.graph();
    graph.add_host(Reads{}, Writes(target), [&released, data]()
    {
        wait_for_rules_gate(released);
        data[0] = 7;
    });

    auto step = graph.run_async().value();

    PlacementRules rules;
    ASSERT_TRUE(rules.add(rule_for("sushiai.replica", std::nullopt, 1)));
    const Result<void> set = rt.set_placement_rules(std::move(rules));
    const Result<void> cleared = rt.clear_placement_rules();

    released.store(true, std::memory_order_release);
    step.wait().value();

    ASSERT_FALSE(set.has_value());
    EXPECT_EQ(set.error().code, ErrorCode::InvalidState);
    EXPECT_NE(set.error().message.find("wait"), std::string::npos) << set.error().message;
    ASSERT_FALSE(cleared.has_value());
    EXPECT_EQ(cleared.error().code, ErrorCode::InvalidState);

    PlacementRules after;
    ASSERT_TRUE(after.add(rule_for("sushiai.replica", std::nullopt, 1)));
    EXPECT_TRUE(rt.set_placement_rules(std::move(after))) << "the run ended, so the call passes";
    EXPECT_TRUE(rt.clear_placement_rules());
}

/**
 * @brief Tests that a rule change racing runs and scope reads never lands inside a run.
 *
 * Every shared value is an atomic or a std::atomic_load of the rules, so TSan sees no race.
 */
TEST(Medium_RuntimePlacementRules, Regression_ARuleChangeRacingARunStartNeverLandsInsideTheRun)
{
    const Runtime rt = Runtime::create().value();
    Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(rt);
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> changed_inside_a_run{0};
    std::atomic<std::uint64_t> applied{0};

    auto target = rt.buffer<int>(1).value();
    auto graph = rt.graph();
    graph.add_host(Reads{}, Writes(target), [&context, &changed_inside_a_run]()
    {
        const std::uint64_t at_start = context.placement_generation();
        std::this_thread::yield();
        if (context.placement_generation() != at_start)
            changed_inside_a_run.fetch_add(1, std::memory_order_relaxed);
    });

    std::thread writer([&rt, &stop, &applied]()
    {
        for (int round = 0; !stop.load(std::memory_order_acquire); ++round)
        {
            PlacementRules rules;
            EXPECT_TRUE(rules.add(rule_for("sushiai.replica", std::nullopt, 1)));
            const Result<void> done = (round % 2 == 0) ? rt.set_placement_rules(std::move(rules))
                                                       : rt.clear_placement_rules();
            if (done)
                applied.fetch_add(1, std::memory_order_relaxed);
            else
                EXPECT_EQ(done.error().code, ErrorCode::InvalidState);
        }
    });
    std::vector<std::thread> readers;
    for (int reader = 0; reader < 2; ++reader)
        readers.emplace_back([&rt, &stop]()
        {
            const PlacementScope scope = rt.placement_scope("sushiai.replica");
            while (!stop.load(std::memory_order_acquire))
                (void)scope.device();
        });

    // EXPECT, not ASSERT: the threads must be joined whatever a run returns.
    for (int run = 0; run < 2000; ++run)
        EXPECT_TRUE(graph.run());
    stop.store(true, std::memory_order_release);
    writer.join();
    for (std::thread& reader : readers)
        reader.join();

    EXPECT_EQ(changed_inside_a_run.load(), 0u);
    EXPECT_GT(applied.load(), 0u) << "the writer never found the runtime idle";
}

/** @brief Tests that create() refuses an empty key, a key holding '=' and a repeated key. */
TEST(Medium_RuntimeEntry, CreateRefusesALabelKeyThatBreaksARule)
{
    struct Case
    {
        std::vector<NodeLabel> labels;
        std::string named;
    };
    const std::vector<Case> cases = {
        {{NodeLabel{"zone", "a"}, NodeLabel{"", "b"}}, "position 1"},
        {{NodeLabel{"a=b", "c"}}, "\"a=b\""},
        {{NodeLabel{"zone", "a"}, NodeLabel{"rack", "b"}, NodeLabel{"zone", "c"}}, "\"zone\""},
    };

    for (const Case& refused_case : cases)
    {
        RuntimeConfig config;
        config.labels = refused_case.labels;

        const auto refused = Runtime::create(config);
        ASSERT_FALSE(refused.has_value()) << refused_case.named;
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument) << refused_case.named;
        EXPECT_NE(refused.error().message.find(refused_case.named), std::string::npos)
            << refused.error().message;
        EXPECT_NE(refused.error().message.find("RuntimeConfig::labels"), std::string::npos)
            << refused.error().message;
    }
}

/** @brief Tests that create() with no argument yields a runtime that runs a graph. */
TEST(Medium_RuntimeEntry, DefaultConfigCreates)
{
    constexpr std::size_t COUNT = 64;

    const Runtime rt = Runtime::create().value();
    EXPECT_FALSE(rt.advanced().is_profiling_enabled());
    EXPECT_FALSE(rt.advanced().is_rebalancer_enabled());

    auto values = rt.buffer<int>(COUNT).value();
    int* data = values.data();
    for (std::size_t i = 0; i < COUNT; ++i)
        values[i] = 0;

    auto graph = rt.graph();
    graph.add(COUNT, Reads(), Writes(values), [data](std::size_t i) { data[i] = 7; });
    graph.run().value();

    for (std::size_t i = 0; i < COUNT; ++i)
        ASSERT_EQ(values[i], 7) << "element " << i;
}

/** @brief Tests that every RuntimeConfig field reaches the runtime it configures. */
TEST(Medium_RuntimeEntry, EveryFieldReachesTheRuntime)
{
    RuntimeConfig config;
    config.worker_count = 1;
    config.pinning = PinPolicy::Float;
    config.profiling = true;
    config.rebalancer = true;
    config.force_scheduler = true;

    const Runtime rt = Runtime::create(config).value();
    const AdvancedControls view = rt.advanced();
    const Internal::Execution::RuntimeSettings& settings =
        SushiRuntime::Internal::ContextAccess::context(rt).settings();

    EXPECT_EQ(view.worker_pool().count, 1u);
    EXPECT_EQ(view.worker_pool().pinning, PinPolicy::Float);
    EXPECT_TRUE(view.is_profiling_enabled());
    EXPECT_TRUE(view.is_rebalancer_enabled());
    EXPECT_TRUE(settings.force_scheduler);

    rt.set_rebalancer_enabled(false);
    EXPECT_FALSE(view.is_rebalancer_enabled());
}

/** @brief Tests that a moved Runtime keeps the context its handles were made from. */
TEST(Medium_RuntimeEntry, AMovedRuntimeKeepsItsContext)
{
    Runtime first = Runtime::create().value();
    Internal::Execution::RuntimeContext* const context =
        &SushiRuntime::Internal::ContextAccess::context(first);

    const Runtime second = std::move(first);

    EXPECT_EQ(&SushiRuntime::Internal::ContextAccess::context(second), context);
}

/** @brief Tests that a caller waits for device work and copies through the device's queue. */
TEST(Medium_RuntimeEntry, ACallerWaitsAndCopiesThroughTheDeviceQueue)
{
    constexpr std::size_t K_WORDS = 256;

    const Runtime runtime = Runtime::create().value();
    const Buffer<std::uint32_t> values = runtime.buffer<std::uint32_t>(K_WORDS).value();
    std::uint32_t* words = values.data();

    sycl::queue& queue = runtime.advanced().queue(values.device_index());
    queue.parallel_for(sycl::range<1>(K_WORDS), [words](sycl::id<1> i)
    {
        words[i] = static_cast<std::uint32_t>(i[0]) + 1u;
    });
    runtime.wait().value();

    std::vector<std::uint32_t> copied(K_WORDS, 0u);
    queue.memcpy(copied.data(), words, K_WORDS * sizeof(std::uint32_t)).wait();

    for (std::size_t i = 0; i < K_WORDS; ++i)
        EXPECT_EQ(copied[i], static_cast<std::uint32_t>(i) + 1u) << "word " << i;
}

/** @brief Tests that asking for the queue of a device the runtime does not have stops. */
TEST(Medium_RuntimeEntryDeathTest, AQueueOfAnUnknownDeviceStops)
{
    const Runtime runtime = Runtime::create().value();
    const AdvancedControls controls = runtime.advanced();

    EXPECT_DEATH(static_cast<void>(controls.queue(DeviceIndex{controls.device_count()})),
                 "Device index .* is out of bounds");
}

/** @brief Tests that a buffer and a state no device can hold come back as AllocationFailed. */
TEST(Medium_RuntimeEntry, ARequestNoDeviceCanSatisfyReturnsAllocationFailed)
{
    const Runtime runtime = Runtime::create().value();
    // Half the address space in bytes: the count fits std::size_t and no device has the room.
    const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

    const auto buffer = runtime.buffer<std::uint8_t>(count);
    ASSERT_FALSE(buffer.has_value());
    EXPECT_EQ(buffer.error().code, ErrorCode::AllocationFailed);
    EXPECT_NE(buffer.error().message.find(std::to_string(count) + " elements"),
              std::string::npos)
        << buffer.error().message;

    const auto state = runtime.state<std::uint8_t>(count);
    ASSERT_FALSE(state.has_value());
    EXPECT_EQ(state.error().code, ErrorCode::AllocationFailed);

    const auto small = runtime.buffer<std::uint8_t>(16);
    EXPECT_TRUE(small.has_value()) << "a failed allocation must leave the runtime usable";
}

/** @brief Tests that the live allocation count rises with a buffer and falls when it is freed. */
TEST(Medium_RuntimeEntry, LiveAllocationCountFollowsABuffer)
{
    auto rt = Runtime::create().value();
    const AdvancedControls controls = rt.advanced();
    const std::size_t before = controls.live_allocation_count();

    {
        const auto buf = rt.buffer<int>(8).value();
        EXPECT_EQ(controls.live_allocation_count(), before + 1);

        const auto field = rt.state<int>(8).value();
        EXPECT_EQ(controls.live_allocation_count(), before + 3) << "a State holds two fields";
    }

    EXPECT_EQ(controls.live_allocation_count(), before);
}

/**
 * @brief Tests that a thread that outlives the Runtime it allocated from tears down without a
 *        fault.
 */
TEST(Medium_Runtime, AThreadOutlivingTheRuntimeTearsDownCleanly)
{
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool allocated = false;
    bool runtime_gone = false;

    std::thread outliver;

    {
        auto rt = SushiRuntime::Runtime::create().value();

        outliver = std::thread([&]()
        {
            // The thread's exit-time destructors touch per-thread runtime state; see README.md.
            {
                auto scratch = rt.buffer<int>(4096).value();
                for (std::size_t i = 0; i < 64; ++i)
                    scratch[i] = static_cast<int>(i);
            }

            {
                std::lock_guard<std::mutex> lock(gate_mutex);
                allocated = true;
            }
            gate.notify_one();

            // Stay alive until the Runtime — and its allocator — are gone.
            std::unique_lock<std::mutex> lock(gate_mutex);
            gate.wait(lock, [&] { return runtime_gone; });
        });

        std::unique_lock<std::mutex> lock(gate_mutex);
        gate.wait(lock, [&] { return allocated; });
    }   // Runtime destroyed here, while the other thread is still alive.

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        runtime_gone = true;
    }
    gate.notify_one();

    // Reaching the line after this join without a fault is the assertion; see README.md.
    outliver.join();
    SUCCEED() << "a thread unwound after the Runtime it allocated from was gone";
}

/** @brief Tests that Runtime::create() prints nothing at Warn and logs its devices at Info. */
namespace LoggingDefaultsTests
{
    namespace
    {
        /** @brief Holds one record a test sink received: its level and a copy of its line. */
        using SeenRecord = std::pair<SushiLog::LogLevel, std::string>;

        /** @brief Holds the two process streams as captured around one call. */
        struct CapturedStreams
        {
            std::string standard_output; /**< What reached stdout. */
            std::string standard_error;  /**< What reached stderr. */
        };

        /** @brief Creates and destroys a default Runtime and returns what the streams received. */
        CapturedStreams capture_streams_around_create()
        {
            CapturedStreams captured;
            testing::internal::CaptureStdout();
            testing::internal::CaptureStderr();
            {
                auto runtime = Runtime::create().value();
                EXPECT_GE(runtime.advanced().device_count(), 1u);
            }
            captured.standard_error = testing::internal::GetCapturedStderr();
            captured.standard_output = testing::internal::GetCapturedStdout();
            return captured;
        }

        /** @brief Tells whether a line starts the way the logger writes one: a time, then a level. */
        bool is_logger_line(const std::string& line)
        {
            static const std::regex prefix(R"(^\[\d{2}:\d{2}:\d{2}\.\d{3}\] \[)");
            return std::regex_search(line, prefix);
        }

        /** @brief Returns the lines of @p text the logger wrote, dropping any other output. */
        std::string logger_lines_only(const std::string& text)
        {
            std::string kept;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
            {
                if (is_logger_line(line))
                    kept += line + '\n';
            }
            return kept;
        }

        /** @brief Puts the logger in synchronous mode and restores the level and the sink. */
        class Medium_LoggingDefaults : public ::testing::Test
        {
            protected:
                void SetUp() override
                {
                    saved_level_ = SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT);
                    SushiLog::set_log_synchronous(true);
                    SushiLog::set_log_sink(nullptr);
                }

                void TearDown() override
                {
                    SushiLog::set_log_sink(nullptr);
                    SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, saved_level_);
                }

            private:
                /// @brief The level the process had when the test began.
                SushiLog::LogLevel saved_level_ = SushiLog::LogLevel::Warn;
        };
    }

    /**
     * @brief Tests that the default sink writes no logger line while a Runtime is created at Warn.
     *
     * Output that is not the logger's, such as a SYCL device sanitizer banner printed under
     * ThreadSanitizer, is not the logger's to silence and is ignored on stderr.
     */
    TEST_F(Medium_LoggingDefaults, CreateIsSilentAtTheDefaultLevel)
    {
        SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Warn);

        const CapturedStreams captured = capture_streams_around_create();

        EXPECT_TRUE(captured.standard_output.empty()) << captured.standard_output;
        EXPECT_TRUE(logger_lines_only(captured.standard_error).empty()) << captured.standard_error;
    }

    /** @brief Tests that at Info the device line reaches the sink and no logger line reaches stderr. */
    TEST_F(Medium_LoggingDefaults, DeviceLineArrivesThroughTheSinkAtInfo)
    {
        std::vector<SeenRecord> records;
        SushiLog::set_log_sink([&records](const SushiLog::LogRecord& record)
        {
            records.emplace_back(record.level, std::string(record.text));
        });
        SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Info);

        const CapturedStreams captured = capture_streams_around_create();
        SushiLog::set_log_sink(nullptr);

        bool saw_device_line = false;
        for (const SeenRecord& record : records)
        {
            if (record.first == SushiLog::LogLevel::Info &&
                record.second.find("Device: ") != std::string::npos)
                saw_device_line = true;
        }

        EXPECT_TRUE(saw_device_line) << records.size() << " records, none an Info device line";
        EXPECT_TRUE(captured.standard_output.empty()) << captured.standard_output;
        EXPECT_TRUE(logger_lines_only(captured.standard_error).empty()) << captured.standard_error;
    }
} // namespace LoggingDefaultsTests
