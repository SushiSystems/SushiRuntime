/**************************************************************************/
/* test_replay.cpp                                                        */
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
 * @file test_replay.cpp
 * @brief Tests that a graph which allows a replay leaves the bytes of layer 1, and which layer runs it.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiLog/log/log_level.hpp>
#include <SushiRuntime/execution/chain_replay.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

#include "run_recorder.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Tests that a graph which allows a replay leaves the bytes of layer 1, and which layer runs it. */
namespace ReplayTests
{
    namespace
    {
        /** @brief Names the element type of every buffer here; unsigned, so overflow wraps. */
        using Word = std::uint32_t;

        constexpr std::size_t K_N = 256;
        constexpr std::size_t K_MAX_BASE = 7;
        constexpr std::size_t K_STEPS = 24;
        constexpr std::size_t K_RUNS = 3;

        constexpr DynamicGraph::RegionKey K_KEY_FIXED = 10;
        constexpr DynamicGraph::RegionKey K_KEY_WINDOW = 20;
        constexpr DynamicGraph::RegionKey K_KEY_COUNTED = 30;
        constexpr DynamicGraph::RegionKey K_KEY_LATE = 40;

        /** @brief Holds the calls of one workload's per-step providers, one count per provider. */
        struct StepControls
        {
            std::size_t when_calls = 0;  /**< Calls of the enabled provider. */
            std::size_t count_calls = 0; /**< Calls of the count provider. */
            std::size_t base_calls = 0;  /**< Calls of the base provider. */
        };

        /**
         * @brief Returns a window whose enabled flag, count and base change at every step.
         * @param controls Outlives every run of the node the window is recorded on.
         */
        Dynamic changing_window(StepControls& controls)
        {
            Dynamic window = when([&controls] { return (controls.when_calls++ % 3) != 2; });
            window.sized([&controls] { return (controls.count_calls++ * 37 + 5) % (K_N - K_MAX_BASE + 1); });
            window.based_at([&controls] { return controls.base_calls++ % (K_MAX_BASE + 1); });
            return window;
        }

        /** @brief Returns a buffer of K_N words seeded with their index. */
        Buffer<Word> seeded(Runtime& rt)
        {
            Buffer<Word> buffer = rt.buffer<Word>(K_N).value();
            for (std::size_t i = 0; i < K_N; ++i)
                buffer[i] = static_cast<Word>(i);
            return buffer;
        }

        /** @brief Returns a copy of a buffer's words. */
        std::vector<Word> words_of(const Buffer<Word>& buffer)
        {
            std::vector<Word> copied(K_N);
            for (std::size_t i = 0; i < K_N; ++i)
                copied[i] = buffer[i];
            return copied;
        }

        /** @brief Records a fixed kernel that folds each word of @p acc with its index. */
        template <class Builder>
        void record_fixed(Builder& builder, Buffer<Word>& acc)
        {
            Word* p = acc.data();
            builder.add(K_N, Reads{}, Writes(acc),
                        [p](std::size_t i) { p[i] = p[i] * 3u + static_cast<Word>(i) + 1u; });
        }

        /** @brief Records a late-bound kernel whose window changes at every step. */
        template <class Builder>
        void record_window(Builder& builder, Buffer<Word>& acc, Buffer<Word>& out, StepControls& controls)
        {
            const Word* a = acc.data();
            Word* o = out.data();
            builder.add_dynamic(changing_window(controls), K_N, Reads(acc), Writes(out),
                                [a, o](std::size_t i) { o[i] = o[i] * 7u + a[i]; });
        }

        /**
         * @brief Returns the layer an opted-in plan runs on here: a replay where replays_on() admits
         *        device 0, as the run itself decides, else a chain.
         */
        ExecutionLayer replay_layer(Runtime& rt)
        {
            const sycl::device device = rt.advanced().queue(DeviceIndex{0}).get_device();
            return SushiRuntime::Internal::Execution::replays_on(device) ? ExecutionLayer::Replay
                                                                          : ExecutionLayer::Chain;
        }

        /** @brief Returns how many records of a capture hold @p text in their message. */
        std::size_t count_messages(const TestRunner::LogCapture& capture, const std::string& text)
        {
            std::size_t count = 0;
            for (const TestRunner::CapturedRecord& record : capture.records())
                if (record.message.find(text) != std::string::npos)
                    ++count;
            return count;
        }

        /** @brief Owns a Graph of a fixed kernel and a late-bound kernel, with or without a replay. */
        class GraphWorkload final
        {
            public:
                /** @brief Records the two kernels on fresh seeded buffers. */
                GraphWorkload(Runtime& rt, bool replay)
                    : acc_(seeded(rt)), out_(seeded(rt)), graph_(rt.graph())
                {
                    record_fixed(graph_, acc_);
                    record_window(graph_, acc_, out_, controls_);
                    graph_.allow_replay(replay);
                }

                GraphWorkload(const GraphWorkload&) = delete;
                GraphWorkload& operator=(const GraphWorkload&) = delete;
                GraphWorkload(GraphWorkload&&) = delete;
                GraphWorkload& operator=(GraphWorkload&&) = delete;
                ~GraphWorkload() = default;

                /** @brief Runs @p steps steps and returns the layer the report names. */
                ExecutionLayer run(std::size_t steps) { return graph_.run(steps).value().layer; }

                /** @brief Returns both buffers' words, the accumulator first. */
                std::vector<Word> image() const
                {
                    std::vector<Word> words = words_of(acc_);
                    const std::vector<Word> out = words_of(out_);
                    words.insert(words.end(), out.begin(), out.end());
                    return words;
                }

            private:
                Buffer<Word> acc_;      /**< Written by the fixed kernel, read by the window. */
                Buffer<Word> out_;      /**< Written by the window. */
                StepControls controls_; /**< The window's providers; the graph reads them by reference. */
                Graph graph_;           /**< Last, so it is destroyed before what it reads. */
        };

        /** @brief Owns a DynamicGraph of the same two kernels in two regions, on the chain, with or without a replay. */
        class DynamicWorkload final
        {
            public:
                /** @brief Records the two kernels on fresh seeded buffers, each in its own region. */
                DynamicWorkload(Runtime& rt, bool replay)
                    : acc_(seeded(rt)), out_(seeded(rt)), world_(rt.dynamic_graph())
                {
                    record_fixed(world_.region(K_KEY_FIXED), acc_);
                    record_window(world_.region(K_KEY_WINDOW), acc_, out_, controls_);
                    world_.allow_chain(true);
                    world_.allow_replay(replay);
                }

                DynamicWorkload(const DynamicWorkload&) = delete;
                DynamicWorkload& operator=(const DynamicWorkload&) = delete;
                DynamicWorkload(DynamicWorkload&&) = delete;
                DynamicWorkload& operator=(DynamicWorkload&&) = delete;
                ~DynamicWorkload() = default;

                /** @brief Runs @p steps steps and returns the layer the report names. */
                ExecutionLayer run(std::size_t steps) { return world_.run(steps).value().layer; }

                /** @brief Returns both buffers' words, the accumulator first. */
                std::vector<Word> image() const
                {
                    std::vector<Word> words = words_of(acc_);
                    const std::vector<Word> out = words_of(out_);
                    words.insert(words.end(), out.begin(), out.end());
                    return words;
                }

            private:
                Buffer<Word> acc_;      /**< Written by the fixed kernel, read by the window. */
                Buffer<Word> out_;      /**< Written by the window. */
                StepControls controls_; /**< The window's providers; the graph reads them by reference. */
                DynamicGraph world_;    /**< Last, so it is destroyed before what it reads. */
        };

        /** @brief Asserts two images hold the same bytes. */
        void expect_same_bytes(const std::vector<Word>& expected, const std::vector<Word>& actual, std::size_t run)
        {
            ASSERT_EQ(expected.size(), actual.size());
            EXPECT_EQ(std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(Word)), 0)
                << "the replayed bytes differ from layer 1's after run " << run;
        }
    } // namespace

    /** @brief Fixture of the replay tests, which need the program's device runtime. */
    class Medium_Replay : public TestSupport::RuntimeTest
    {
    };

    /** @brief Tests that a replayed Graph leaves layer 1's bytes over many steps whose window changes at each. */
    TEST_F(Medium_Replay, AReplayedGraphLeavesTheBytesOfTheChain)
    {
        GraphWorkload replayed(runtime(), true);
        GraphWorkload chained(runtime(), false);
        for (std::size_t run = 0; run < K_RUNS; ++run)
        {
            EXPECT_EQ(replayed.run(K_STEPS), replay_layer(runtime()));
            EXPECT_EQ(chained.run(K_STEPS), ExecutionLayer::Chain);
            expect_same_bytes(chained.image(), replayed.image(), run);
        }
    }

    /** @brief Tests that a replayed DynamicGraph leaves layer 1's bytes over many steps whose window changes at each. */
    TEST_F(Medium_Replay, AReplayedDynamicGraphLeavesTheBytesOfTheChain)
    {
        DynamicWorkload replayed(runtime(), true);
        DynamicWorkload chained(runtime(), false);
        for (std::size_t run = 0; run < K_RUNS; ++run)
        {
            EXPECT_EQ(replayed.run(K_STEPS), replay_layer(runtime()));
            EXPECT_EQ(chained.run(K_STEPS), ExecutionLayer::Chain);
            expect_same_bytes(chained.image(), replayed.image(), run);
        }
    }

    /** @brief Tests that a graph that never called allow_replay() runs on the chain. */
    TEST_F(Medium_Replay, AGraphNotAllowedToReplayRunsOnTheChain)
    {
        Buffer<Word> acc = seeded(runtime());
        auto g = runtime().graph();
        record_fixed(g, acc);
        EXPECT_EQ(g.run(K_STEPS).value().layer, ExecutionLayer::Chain);
    }

    /** @brief Tests that a recomposition drops the recorded step and the next run records the new plan once. */
    TEST_F(Medium_Replay, ARecompositionRecordsTheNewPlanOnce)
    {
        if (replay_layer(runtime()) != ExecutionLayer::Replay)
            GTEST_SKIP() << "the runtime's backend does not replay";

        Buffer<Word> marks = runtime().buffer<Word>(K_N).value();
        Buffer<Word> acc = seeded(runtime());
        for (std::size_t i = 0; i < K_N; ++i)
            marks[i] = 0u;
        Word* p = marks.data();
        std::size_t calls = 0;

        auto world = runtime().dynamic_graph();
        world.region(K_KEY_COUNTED).add(Reads{}, Writes(marks), [&calls, p](sycl::handler& h)
        {
            ++calls;
            h.parallel_for(sycl::range<1>(K_N), [p](sycl::id<1> i) { p[i[0]] += 1u; });
        });
        world.allow_chain(true).allow_replay(true);

        EXPECT_EQ(world.run(K_STEPS).value().layer, ExecutionLayer::Replay);
        EXPECT_EQ(world.run(K_STEPS).value().layer, ExecutionLayer::Replay);
        EXPECT_EQ(calls, 1u) << "two runs of one composition replay one recording";
        const std::size_t composed = world.compose_count();

        record_fixed(world.region(K_KEY_LATE), acc);
        EXPECT_EQ(world.run(K_STEPS).value().layer, ExecutionLayer::Replay);
        EXPECT_EQ(world.compose_count(), composed + 1);
        EXPECT_EQ(calls, 2u) << "the new composition records once";
        for (std::size_t i = 0; i < K_N; ++i)
            ASSERT_EQ(marks[i], static_cast<Word>(3 * K_STEPS)) << "at i=" << i;
    }

    /** @brief Tests that a recording that throws keeps the plan on the chain for its lifetime and logs once. */
    TEST_F(Medium_Replay, AFailedRecordingFallsBackToTheChainAndLogsOnce)
    {
        if (replay_layer(runtime()) != ExecutionLayer::Replay)
            GTEST_SKIP() << "the runtime's backend does not replay, so nothing is recorded";

        Buffer<Word> marks = runtime().buffer<Word>(K_N).value();
        for (std::size_t i = 0; i < K_N; ++i)
            marks[i] = 0u;
        Word* p = marks.data();
        std::size_t calls = 0;

        auto g = runtime().graph();
        // The first call of this command group is the recording's, and it throws.
        g.add(Reads{}, Writes(marks), [&calls, p](sycl::handler& h)
        {
            if (calls++ == 0)
                throw std::runtime_error("forced recording failure");
            h.parallel_for(sycl::range<1>(K_N), [p](sycl::id<1> i) { p[i[0]] += 1u; });
        });
        g.allow_replay(true);

        const TestRunner::LogCapture capture;
        for (std::size_t run = 0; run < K_RUNS; ++run)
            EXPECT_EQ(g.run(K_STEPS).value().layer, ExecutionLayer::Chain);

        EXPECT_EQ(count_messages(capture, "as a command graph failed"), 1u);
        for (std::size_t i = 0; i < K_N; ++i)
            ASSERT_EQ(marks[i], static_cast<Word>(K_RUNS * K_STEPS)) << "at i=" << i;
    }

    /** @brief Tests that a timed run of a graph that replays runs on the chain, and the next untimed run replays. */
    TEST_F(Medium_Replay, ATimedRunReportsTheChain)
    {
        GraphWorkload replayed(runtime(), true);
        GraphWorkload chained(runtime(), false);
        EXPECT_EQ(replayed.run(K_STEPS), replay_layer(runtime()));
        EXPECT_EQ(chained.run(K_STEPS), ExecutionLayer::Chain);
        {
            const Testing::RunRecorder observed(runtime());
            EXPECT_EQ(replayed.run(K_STEPS), ExecutionLayer::Chain);
            ASSERT_GE(observed.count(), 1u);
            EXPECT_EQ(observed.last().layer, ExecutionLayer::Chain);
            EXPECT_EQ(chained.run(K_STEPS), ExecutionLayer::Chain);
        }
        EXPECT_EQ(replayed.run(K_STEPS), replay_layer(runtime()));
        EXPECT_EQ(chained.run(K_STEPS), ExecutionLayer::Chain);
        expect_same_bytes(chained.image(), replayed.image(), 2);
    }

    /** @brief Tests that RuntimeConfig::force_chain keeps a graph that allows a replay on the chain. */
    TEST_F(Medium_Replay, TheForceChainFlagOfTheConfigKeepsAReplayOnTheChain)
    {
        TestSupport::RuntimeShape shape;
        shape.force_chain = true;
        Runtime& forced = TestSupport::RuntimeEnvironment::runtime(shape);
        GraphWorkload replayed(forced, true);
        EXPECT_EQ(replayed.run(K_STEPS), ExecutionLayer::Chain);
    }

    /** @brief Tests that a State rule keeps a plan that allows a replay on the chain, with the reason logged. */
    TEST_F(Medium_Replay, AStateRuleIsRefusedWithItsReason)
    {
        State<Word> field = runtime().state<Word>(K_N).value();
        for (std::size_t i = 0; i < K_N; ++i)
        {
            field[i] = static_cast<Word>(i);
            field.next()[i] = 0u;
        }
        auto g = runtime().graph();
        g.add_rule(field, [](std::size_t i, const Word* current, Word* next) { next[i] = current[i] + 1u; });
        g.allow_replay(true);

        const TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);
        EXPECT_EQ(g.run(K_STEPS).value().layer, ExecutionLayer::Chain);
        EXPECT_EQ(count_messages(capture, "not replayed: a node reads host state"), 1u);
        EXPECT_EQ(field[0], static_cast<Word>(K_STEPS));
    }

    /** @brief Tests that a task body keeps a plan that allows a replay on the chain, with the reason logged. */
    TEST_F(Medium_Replay, ATaskBodyIsRefusedWithItsReason)
    {
        Buffer<Word> marks = runtime().buffer<Word>(K_N).value();
        for (std::size_t i = 0; i < K_N; ++i)
            marks[i] = 0u;
        Word* p = marks.data();

        auto g = runtime().graph();
        g.add_task(Reads{}, Writes(marks), [p](sycl::queue& queue, const std::vector<sycl::event>& after)
        {
            return queue.submit([&after, p](sycl::handler& h)
            {
                h.depends_on(after);
                h.parallel_for(sycl::range<1>(K_N), [p](sycl::id<1> i) { p[i[0]] += 1u; });
            });
        });
        g.allow_replay(true);

        const TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);
        EXPECT_EQ(g.run(K_STEPS).value().layer, ExecutionLayer::Chain);
        EXPECT_EQ(count_messages(capture, "not replayed: a node is a task body"), 1u);
        EXPECT_EQ(marks[0], static_cast<Word>(K_STEPS));
    }
} // namespace ReplayTests
