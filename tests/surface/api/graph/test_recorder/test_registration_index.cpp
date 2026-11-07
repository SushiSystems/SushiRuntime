/**************************************************************************/
/* test_registration_index.cpp                                            */
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
 * @file test_registration_index.cpp
 * @brief Tests that the nodes of one rule scope share one registration index in the node table of a run.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "recorder_fixtures.hpp"
#include "run_recorder.hpp"

using namespace SushiRuntime;

/** @brief Tests that the nodes of one rule scope share one registration index in the node table of a run. */
namespace RegistrationIndexTests
{
    namespace
    {
        constexpr std::size_t K_WIDTH = 64;
        constexpr std::size_t K_RULE_NODES = 3;

        /** @brief Holds the three buffers the grouped rules pass their values through. */
        struct Stages
        {
            Buffer<std::uint32_t> first;  /**< The rule's input. */
            Buffer<std::uint32_t> second; /**< What the rule's first kernel writes. */
            Buffer<std::uint32_t> third;  /**< The rule's output. */
        };

        /** @brief Allocates the stage buffers and seeds the first with its indices. */
        Stages make_stages(const Runtime& rt)
        {
            Stages s{rt.buffer<std::uint32_t>(K_WIDTH).value(),
                     rt.buffer<std::uint32_t>(K_WIDTH).value(),
                     rt.buffer<std::uint32_t>(K_WIDTH).value()};
            for (std::size_t i = 0; i < K_WIDTH; ++i)
            {
                s.first[i] = static_cast<std::uint32_t>(i);
                s.second[i] = 0;
                s.third[i] = 0;
            }
            return s;
        }

        /**
         * @brief Records K_RULE_NODES kernels in one rule scope, each reading what the last wrote.
         * @param stages Buffers of K_WIDTH words; must outlive the recorder.
         */
        void record_grouped_rule(Recorder& recorder, const Stages& stages)
        {
            std::uint32_t* a = stages.first.data();
            std::uint32_t* b = stages.second.data();
            std::uint32_t* c = stages.third.data();

            auto rule = recorder.rule();
            recorder.kernel(K_WIDTH, Access(Reads(stages.first), Writes(stages.second)),
                            [a, b](std::size_t i) { b[i] = a[i] + 1u; });
            recorder.kernel(K_WIDTH, Access(Reads(stages.second), Writes(stages.third)),
                            [b, c](std::size_t i) { c[i] = b[i] * 2u; });
            recorder.kernel(K_WIDTH, Access(Reads(stages.third), Writes(stages.first)),
                            [c, a](std::size_t i) { a[i] = c[i]; });
        }

        /**
         * @brief Records a resolver, a device-sized kernel and a closing kernel in one rule scope.
         * @param stages Buffers of K_WIDTH words; must outlive the recorder.
         * @param count  Live width of the device-sized kernel; its capacity is at most K_WIDTH.
         */
        void record_late_bound_rule(Recorder& recorder, const Stages& stages,
                                    const LateCount& count)
        {
            std::uint32_t* live = recorder.scratch<std::uint32_t>(
                1, stages.first.device_index(), stages.first.residency()).value();
            const BufferRegion live_region{live, 0, sizeof(std::uint32_t)};
            std::uint32_t* a = stages.first.data();
            std::uint32_t* b = stages.second.data();
            std::uint32_t* c = stages.third.data();

            auto rule = recorder.rule();
            recorder.resolve(std::array<LateCount, 1>{count}, stages.first.device_index(),
                             Access(Reads(), Writes(live_region)),
                             [live](const std::array<std::size_t, 1>& value)
                             {
                                 live[0] = static_cast<std::uint32_t>(value[0]);
                             });
            recorder.kernel(DeviceSized{count.capacity(), live, nullptr},
                            Access(Reads(stages.first, live_region), Writes(stages.second)),
                            [a, b](std::size_t t) { b[t] = a[t] + 1u; });
            recorder.kernel(std::size_t{1},
                            Access(Reads(stages.second, live_region), Writes(stages.third)),
                            [b, c, live](std::size_t) { c[0] = b[0] + live[0]; });
        }

        /** @brief The kinds of node a rule scope groups, as the rows of one test. */
        enum class RuleKind
        {
            Plain,     /**< Three plain kernels. */
            LateBound  /**< A resolver, a device-sized kernel and a plain kernel. */
        };

        /** @brief Records a rule of the given kind. */
        void record_rule(Recorder& recorder, const Stages& stages, RuleKind kind)
        {
            if (kind == RuleKind::Plain)
            {
                record_grouped_rule(recorder, stages);
                return;
            }
            const LateCount count = LateCount::from_host(K_WIDTH, []() { return K_WIDTH; });
            record_late_bound_rule(recorder, stages, count);
        }
    } // namespace

    /** @brief Runs the registration-index test of the plain rule on a host runtime. */
    class Small_RegistrationIndex : public RecorderHostTest
    {
    };

    /** @brief Runs the registration-index test of the late-bound rule, whose device-sized kernel needs a device. */
    class Medium_RegistrationIndex : public RecorderTest
    {
    };

    /** @brief Runs the registration-index tests of the plain rule after a rebuild on a host runtime. */
    class Small_RegistrationIndexRebuild : public RecorderHostTest
    {
    };

    /**
     * @brief Expects that a rule scope groups its nodes under one index, between two single-node rules.
     * @param rt   The runtime that runs the graph.
     * @param kind The kind of rule the scope records.
     */
    void expect_rule_scope_groups_its_nodes_under_one_index(const Runtime& rt, RuleKind kind)
    {
        Stages stages = make_stages(rt);
        Testing::RunRecorder observed(rt);

        auto g = rt.graph();
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
        record_rule(g.recorder(), stages, kind);
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
        g.run().value();

        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 3u) << "the three nodes of the rule take one row";

        const NodeTableRow* before = observation.table->find(0u);
        const NodeTableRow* rule = observation.table->find(1u);
        const NodeTableRow* after = observation.table->find(2u);
        ASSERT_NE(before, nullptr);
        ASSERT_NE(rule, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(before->name, "before");
        EXPECT_EQ(after->name, "after") << "'after' must be before+2: the group draws exactly one index.";
        EXPECT_EQ(rule->multiplicity, K_RULE_NODES);
    }

    /** @brief Tests that a plain rule scope groups every node recorded in it under one index. */
    TEST_F(Small_RegistrationIndex, RuleScopeGroupsItsNodesUnderOneIndex)
    {
        expect_rule_scope_groups_its_nodes_under_one_index(runtime(), RuleKind::Plain);
    }

    /** @brief Tests that a late-bound rule scope groups every node recorded in it under one index. */
    TEST_F(Medium_RegistrationIndex, LateBoundRuleScopeGroupsItsNodesUnderOneIndex)
    {
        expect_rule_scope_groups_its_nodes_under_one_index(runtime(), RuleKind::LateBound);
    }

    /** @brief Tests that a rule scope's group survives a rebuild and the indices stay dense. */
    TEST_F(Small_RegistrationIndexRebuild, RuleScopeGroupSurvivesRebuild)
    {
        const Runtime& rt = runtime();
        Stages stages = make_stages(rt);
        Testing::RunRecorder observed(rt);

        auto g = rt.graph();
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
        record_grouped_rule(g.recorder(), stages);
        g.run().value();

        // An add() after the first run dirties the graph, so every spec is replayed; see README.md.
        g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
        g.run().value();

        ASSERT_EQ(observed.count(), 2u);
        const RunObservation observation = observed.last();
        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 3u);
        const auto& rows = observation.table->rows();
        for (std::size_t i = 0; i < rows.size(); ++i)
            EXPECT_EQ(rows[i].index, i) << "registration indices must be dense 0..n-1 after rebuild.";
        EXPECT_EQ(rows[1].multiplicity, K_RULE_NODES);
    }
} // namespace RegistrationIndexTests
