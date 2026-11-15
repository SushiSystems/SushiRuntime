/**************************************************************************/
/* test_run_placement.cpp                                                 */
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
 * @file test_run_placement.cpp
 * @brief Tests a graph's run placement policy: no policy, a Device answer, a Host answer.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/placement/run_description.hpp>
#include <SushiRuntime/placement/run_placement.hpp>
#include <SushiRuntime/placement/run_placement_policy.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Tests a graph's run placement policy on a host runtime. */
namespace RunPlacementTests
{
    namespace
    {
        /** @brief Names the element type of every buffer here; unsigned, so overflow wraps. */
        using Word = std::uint32_t;

        constexpr std::size_t K_N = 64;
        constexpr std::size_t K_STEPS = 5;
        constexpr DynamicGraph::RegionKey K_KEY_SCALE = 1;
        constexpr DynamicGraph::RegionKey K_KEY_FOLD = 2;

        /** @brief Answers from a script of placements, one per run, and keeps what it was shown. */
        class ScriptedPolicy final : public RunPlacementPolicy
        {
            public:
                /** @brief Answers @p script in order, then repeats its last answer. */
                explicit ScriptedPolicy(std::vector<RunPlacement> script) : script_(std::move(script))
                {

                }

                /** @brief Returns the next answer of the script and keeps @p run. */
                [[nodiscard]] RunPlacement place(const RunDescription& run) override
                {
                    seen_.push_back(run);
                    const std::size_t at = seen_.size() - 1;
                    return at < script_.size() ? script_[at] : script_.back();
                }

                /** @brief Returns every description the policy was shown, in order. */
                [[nodiscard]] const std::vector<RunDescription>& seen() const noexcept { return seen_; }

            private:
                std::vector<RunPlacement> script_;  /**< The answers, one per run. */
                std::vector<RunDescription> seen_;  /**< The descriptions shown so far. */
        };

        /** @brief Returns a buffer of K_N words seeded with their index. */
        Buffer<Word> seeded(Runtime& rt)
        {
            Buffer<Word> buffer = rt.buffer<Word>(K_N).value();
            for (std::size_t i = 0; i < K_N; ++i)
                buffer[i] = static_cast<Word>(i);
            return buffer;
        }

        /** @brief Owns a chained DynamicGraph of two kernels in two regions over one buffer. */
        class Workload final
        {
            public:
                /** @brief Records the two kernels on a fresh seeded buffer; the second states a cost. */
                explicit Workload(Runtime& rt) : acc_(seeded(rt)), world_(rt.dynamic_graph())
                {
                    Word* p = acc_.data();
                    NodeOptions scale;
                    scale.width = K_N;
                    world_.region(K_KEY_SCALE).add(K_N, Reads{}, Writes(acc_),
                        [p](std::size_t i) { p[i] = p[i] * 3u + 1u; }, scale);
                    NodeOptions fold = scale;
                    fold.cost = NodeCost{11, 6, 7, OperationKinds::K_GENERIC, NodeDType::Unstated};
                    world_.region(K_KEY_FOLD).add(K_N, Reads{}, Writes(acc_),
                        [p](std::size_t i) { p[i] = p[i] ^ static_cast<Word>(i * 7u); }, fold);
                    world_.allow_chain(true);
                }

                Workload(const Workload&) = delete;
                Workload& operator=(const Workload&) = delete;
                Workload(Workload&&) = delete;
                Workload& operator=(Workload&&) = delete;
                ~Workload() = default;

                /** @brief Returns the graph, to set its policy. */
                DynamicGraph& graph() noexcept { return world_; }

                /** @brief Runs K_STEPS steps and returns the layer the report names. */
                ExecutionLayer run() { return world_.run(K_STEPS).value().layer; }

                /** @brief Returns the buffer's words. */
                std::vector<Word> image() const
                {
                    std::vector<Word> words(K_N);
                    for (std::size_t i = 0; i < K_N; ++i)
                        words[i] = acc_[i];
                    return words;
                }

            private:
                Buffer<Word> acc_;    /**< Written by both kernels. */
                DynamicGraph world_;  /**< Last, so it is destroyed before what it reads. */
        };

        /** @brief Asserts two images hold the same bytes. */
        void expect_same_bytes(const std::vector<Word>& expected, const std::vector<Word>& actual)
        {
            ASSERT_EQ(expected.size(), actual.size());
            EXPECT_EQ(std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(Word)), 0)
                << "the hosted bytes differ from the engine's";
        }
    } // namespace

    /** @brief Fixture of the run placement tests: one host runtime per test. */
    class Small_RunPlacement : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that a graph with no policy, or whose policy was removed, runs as before. */
    TEST_F(Small_RunPlacement, NoPolicyLeavesTheLayerAndTheBytes)
    {
        Workload untouched(runtime());
        Workload removed(runtime());
        auto policy = std::make_shared<ScriptedPolicy>(std::vector<RunPlacement>{RunPlacement::Host});
        removed.graph().set_run_placement_policy(policy).set_run_placement_policy(nullptr);

        EXPECT_EQ(untouched.run(), ExecutionLayer::Scheduler);
        EXPECT_EQ(removed.run(), ExecutionLayer::Scheduler);

        EXPECT_TRUE(policy->seen().empty());
        expect_same_bytes(untouched.image(), removed.image());
    }

    /** @brief Tests that a Device answer takes the layer the graph takes with no policy. */
    TEST_F(Small_RunPlacement, ADeviceAnswerTakesTheLayerOfNoPolicy)
    {
        Workload plain(runtime());
        Workload placed(runtime());
        auto policy = std::make_shared<ScriptedPolicy>(std::vector<RunPlacement>{RunPlacement::Device});
        placed.graph().set_run_placement_policy(policy);

        EXPECT_EQ(placed.run(), plain.run());

        EXPECT_EQ(policy->seen().size(), 1u);
        expect_same_bytes(plain.image(), placed.image());
    }

    /** @brief Tests that a Host answer runs on the calling thread and leaves the engine's bytes. */
    TEST_F(Small_RunPlacement, AHostAnswerRunsOnTheHostWithTheEnginesBytes)
    {
        Workload engine(runtime());
        Workload hosted(runtime());
        auto policy = std::make_shared<ScriptedPolicy>(std::vector<RunPlacement>{RunPlacement::Host});
        hosted.graph().set_run_placement_policy(policy);

        for (std::size_t run = 0; run < 3; ++run)
        {
            EXPECT_EQ(hosted.run(), ExecutionLayer::Host);
            EXPECT_EQ(engine.run(), ExecutionLayer::Scheduler);
            expect_same_bytes(engine.image(), hosted.image());
        }
    }

    /** @brief Tests that the policy is asked at every run and each report names that run's answer. */
    TEST_F(Small_RunPlacement, EachRunRecordsItsOwnPlacement)
    {
        Workload workload(runtime());
        auto policy = std::make_shared<ScriptedPolicy>(std::vector<RunPlacement>{
            RunPlacement::Host, RunPlacement::Device, RunPlacement::Host});
        workload.graph().set_run_placement_policy(policy);

        EXPECT_EQ(workload.run(), ExecutionLayer::Host);
        EXPECT_EQ(workload.run(), ExecutionLayer::Scheduler);
        EXPECT_EQ(workload.run(), ExecutionLayer::Host);
        EXPECT_EQ(policy->seen().size(), 3u);
    }

    /** @brief Tests that the description counts the plan's nodes, steps, widths and stated costs. */
    TEST_F(Small_RunPlacement, TheDescriptionCountsWhatThePlanStates)
    {
        Workload workload(runtime());
        auto policy = std::make_shared<ScriptedPolicy>(std::vector<RunPlacement>{RunPlacement::Device});
        workload.graph().set_run_placement_policy(policy);

        workload.run();

        ASSERT_EQ(policy->seen().size(), 1u);
        const RunDescription& run = policy->seen().front();
        EXPECT_EQ(run.node_count, 2u);
        EXPECT_EQ(run.steps, K_STEPS);
        EXPECT_EQ(run.launched_items, std::uint64_t{2 * K_N});
        EXPECT_EQ(run.unsized_nodes, 0u);
        EXPECT_EQ(run.costed_nodes, 1u);
        EXPECT_EQ(run.declared_operations, 11u);
        EXPECT_EQ(run.declared_bytes, 13u);
    }

    /** @brief Tests that a Host answer for a plan on device memory keeps the run on the engine. */
    TEST_F(Small_RunPlacement, AHostAnswerForAPlanOnDeviceMemoryStaysOnTheEngine)
    {
        Buffer<Word> resident = runtime().buffer<Word>(K_N, DeviceIndex{0}, Residency::Device).value();
        Word* p = resident.data();
        auto world = runtime().dynamic_graph();
        world.region(K_KEY_SCALE).add(K_N, Reads{}, Writes(resident),
                                      [p](std::size_t i) { p[i] = static_cast<Word>(i); });
        world.allow_chain(true);
        auto policy = std::make_shared<ScriptedPolicy>(std::vector<RunPlacement>{RunPlacement::Host});
        world.set_run_placement_policy(policy);

        EXPECT_EQ(world.run().value().layer, ExecutionLayer::Scheduler);
        EXPECT_EQ(policy->seen().size(), 1u);
    }
} // namespace RunPlacementTests
