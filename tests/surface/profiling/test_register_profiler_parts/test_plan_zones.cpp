/**************************************************************************/
/* test_plan_zones.cpp                                                    */
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
 * @file test_plan_zones.cpp
 * @brief Tests the zones the runtime writes while it builds the plan of a graph.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/events/trace_event.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "profiled_pipelines.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiProfiler::PeriodRecord;
using SushiProfiler::RunRecord;

namespace
{
    /** @brief Records two periods around two runs of @p pipeline and no source, so only the runtime's own zones are written. */
    RunRecord record_two_runs(SessionPipeline& pipeline)
    {
        return record_periods_without_source(0, 2, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
                                             {
                                                 EXPECT_TRUE(pipeline.graph().run().has_value());
                                             });
    }
} // namespace

namespace
{
    /** @brief Returns the first tree of @p period that holds a zone called @p name, or null when none does. */
    const SushiProfiler::ZoneTree* tree_with(const RunRecord& record, const PeriodRecord& period, const char* name)
    {
        for (const SushiProfiler::ZoneTree& tree : period.zones)
            if (find_zone(record, tree, name) >= 0)
                return &tree;
        return nullptr;
    }
} // namespace

/** @brief Tests that a build writes plan.compile inside plan.build, and the build covers the compile's time. */
TEST(Medium_PlanZones, ABuildWritesPlanBuildAroundPlanCompile)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    ASSERT_EQ(record.periods.size(), 2u);
    const SushiProfiler::ZoneTree* tree = tree_with(record, record.periods[0], "plan.compile");
    ASSERT_NE(tree, nullptr) << "the first run builds the plan inside the first period";
    const int compile = find_zone(record, *tree, "plan.compile");
    EXPECT_EQ(parent_zone(record, *tree, compile), "plan.build");
    const int build = tree->nodes[static_cast<std::size_t>(compile)].parent;
    ASSERT_GE(build, 0);
    EXPECT_GE(tree->nodes[static_cast<std::size_t>(build)].inclusive_ns,
              tree->nodes[static_cast<std::size_t>(compile)].inclusive_ns)
        << "a parent's inclusive time covers its child's";
}

/** @brief Tests that a build writes the prepare and classify steps in the tree of its compile. */
TEST(Medium_PlanZones, ABuildWritesItsPrepareAndClassifySteps)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    ASSERT_EQ(record.periods.size(), 2u);
    const SushiProfiler::ZoneTree* tree = tree_with(record, record.periods[0], "plan.compile");
    ASSERT_NE(tree, nullptr);
    EXPECT_GE(find_zone(record, *tree, "plan.prepare"), 0);
    EXPECT_GE(find_zone(record, *tree, "plan.classify"), 0);
}

/** @brief Tests that a second run writes no plan.build, because the plan is cached. */
TEST(Medium_PlanZones, TheSecondRunWritesNoBuildBecauseThePlanIsCached)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    ASSERT_EQ(record.periods.size(), 2u);
    EXPECT_EQ(tree_with(record, record.periods[1], "plan.build"), nullptr);
}

/** @brief Tests that building a plan writes the node and root counts as values. */
TEST(Medium_PlanZones, ABuildWritesTheNodeAndRootCountsAsValues)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    const auto nodes = sample_total(record, "graph.nodes");
    const auto roots = sample_total(record, "graph.roots");
    EXPECT_EQ(nodes.second, 1u) << "one build";
    EXPECT_EQ(nodes.first, 3.0) << "the three recorded nodes; the completion sentinel is held apart and not counted";
    EXPECT_EQ(roots.first, 1.0) << "the load node is the only root";
}

namespace
{
    /** @brief Returns the names of the sites of every event @p batch holds, from @p collector's site table. */
    std::set<std::string> drained_site_names(SushiProfiler::Collector& collector,
                                             const SushiProfiler::DrainBatch& batch)
    {
        std::set<std::string> names;
        for (const SushiProfiler::ThreadDrain& thread : batch.threads)
            for (const SushiProfiler::TraceEvent& event : thread.events)
                names.insert(collector.sites().info(SushiProfiler::site_of(event)).name);
        return names;
    }
} // namespace

/** @brief Tests that nothing of the plan is written while the gate is closed. */
TEST(Medium_PlanZones, NoZoneIsWrittenWhileTheGateIsClosed)
{
    SessionPipeline pipeline;
    SushiProfiler::Collector& collector = SushiProfiler::Collector::instance();
    collector.close();
    SushiProfiler::DrainBatch discarded;
    collector.drain(discarded);
    ASSERT_TRUE(collector.open(SushiProfiler::CollectorSettings{}));
    collector.pause();

    EXPECT_TRUE(pipeline.graph().compile().has_value());

    SushiProfiler::DrainBatch batch;
    collector.drain(batch);
    collector.close();
    EXPECT_EQ(drained_site_names(collector, batch).count("plan.build"), 0u) << "a closed gate writes no event";
}

/** @brief Tests that a graph of regions writes region.commit when it composes its plan. */
TEST(Medium_PlanZones, ADynamicGraphWritesRegionCommit)
{
    Runtime& runtime = TestSupport::RuntimeEnvironment::runtime();
    auto buffer = runtime.buffer<float>(64).value();
    float* data = buffer.data();
    auto graph = runtime.dynamic_graph();
    graph.region(0).add(std::size_t{64}, Reads{}, Writes(buffer), [data](std::size_t i) { data[i] = 1.0f; });

    const RunRecord record = record_periods_without_source(
        0, 1, [&graph](std::uint32_t, SushiProfiler::PeriodTimeline&) { EXPECT_TRUE(graph.run().has_value()); });

    ASSERT_EQ(record.periods.size(), 1u);
    EXPECT_TRUE(zone_sites(record, record.periods[0]).count("region.commit"));
}
