/**************************************************************************/
/* test_registration.cpp                                                  */
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
 * @file test_registration.cpp
 * @brief Tests how the runtime registers its source and contributor, and how it adds and removes its observers.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/environment/environment_contributor.hpp>
#include <SushiProfiler/json/json_reader.hpp>
#include <SushiProfiler/registry/registry.hpp>
#include <SushiProfiler/record/span_table.hpp>
#include <SushiProfiler/sites/name_table.hpp>
#include <SushiProfiler/sources/source.hpp>
#include <SushiProfiler/sources/source_context.hpp>
#include <SushiProfiler/sources/span_sink.hpp>

#include <SushiRuntime/profiling/in_memory_profiled_runtime.hpp>
#include <SushiRuntime/profiling/register_profiler_parts.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/report/recording_observer.hpp>

#include "session_pipeline.hpp"

using namespace SushiRuntime;
using SessionTests::SessionPipeline;
using SushiRuntime::RecordingObserver;

namespace
{
    /** @brief Holds what a source needs to begin outside a session. */
    struct SourceHarness
    {
        SushiProfiler::NameTable names;                                  /**< The session's names. */
        std::vector<std::string> tracks;                                 /**< The record's tracks. */
        SushiProfiler::SourceContext context{names, tracks, 0};          /**< What begin() receives. */
    };

    /** @brief Opens the sources' gate as a running session does, and closes it when it leaves scope. */
    struct WantedSources
    {
        WantedSources() noexcept { SushiProfiler::Collector::want_sources(true); }
        ~WantedSources() { SushiProfiler::Collector::want_sources(false); }
        WantedSources(const WantedSources&) = delete;
        WantedSources& operator=(const WantedSources&) = delete;
    };

    /** @brief Makes the registered source of @p registry, which must name `sushiruntime.device`. */
    std::unique_ptr<SushiProfiler::Source> make_source(const SushiProfiler::Registry& registry)
    {
        SushiProfiler::Made<SushiProfiler::Source> made =
            registry.make_source("sushiruntime.device", *SushiProfiler::read_json("{}").value);
        EXPECT_TRUE(made.error.empty()) << made.error;
        return std::move(made.part);
    }
} // namespace

/** @brief Tests that the registry refuses a second registration of the same source name. */
TEST(Small_RegisterProfilerParts, RefusesASecondRegistration)
{
    const std::shared_ptr<ProfiledRuntime> runtime = std::make_shared<InMemoryProfiledRuntime>();
    SushiProfiler::Registry registry;

    const RuntimeProfilerParts first = register_profiler_parts(registry, runtime);
    const RuntimeProfilerParts second = register_profiler_parts(registry, runtime);

    EXPECT_TRUE(first.is_registered());
    EXPECT_FALSE(second.is_registered());
    EXPECT_NE(make_source(registry), nullptr);
    EXPECT_EQ(registry.contributors().size(), 1u) << "the refused parts added no contributor";
}

/** @brief Tests that the registered source is the one the registry knows by the runtime's source name. */
TEST(Small_RegisterProfilerParts, TheRegistryMakesTheDeviceSource)
{
    const std::shared_ptr<ProfiledRuntime> runtime = std::make_shared<InMemoryProfiledRuntime>();
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime);

    const std::unique_ptr<SushiProfiler::Source> source = make_source(registry);

    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->name(), "sushiruntime.device");
}

namespace
{
    /** @brief Holds a pipeline whose parts are registered and the device source the registry makes, outside a session. */
    struct RegisteredSource
    {
        SessionPipeline pipeline;                                  /**< The runtime and its graph. */
        SushiProfiler::Registry registry;                          /**< Holds the registered source. */
        RuntimeProfilerParts parts{register_profiler_parts(registry, pipeline.runtime())}; /**< The registration. */
        std::unique_ptr<SushiProfiler::Source> source{make_source(registry)};              /**< The source it makes. */
        SourceHarness harness;                                     /**< What the source begins on. */
        SushiProfiler::SpanTable table;                            /**< The period's spans. */
        SushiProfiler::SpanSink sink{table, harness.names, harness.tracks}; /**< The sink collect writes to. */
    };

    /** @brief The period that holds every span. */
    constexpr SushiProfiler::PeriodSpan K_EVERYTHING{0, 0, ~std::uint64_t{0}};
} // namespace

/** @brief Tests that a session adds the observer when its source begins, so a run reaches the source with its records. */
TEST(Medium_RegisterProfilerParts, ASessionAddsTheObserverWhenItsSourceBegins)
{
    RegisteredSource registered;
    const WantedSources wanted;
    ASSERT_TRUE(registered.pipeline.graph().run().has_value());
    ASSERT_EQ(registered.pipeline.graph().compile_count(), 1u);

    ASSERT_TRUE(registered.source->begin(registered.harness.context).ok());
    ASSERT_TRUE(registered.pipeline.graph().run().has_value());
    registered.source->collect(K_EVERYTHING, registered.sink);

    EXPECT_EQ(registered.table.size(), 3u);
    EXPECT_EQ(registered.pipeline.graph().compile_count(), 1u) << "and no plan was rebuilt for it";
    registered.source->end();
}

/** @brief Tests that a session removes the observer when its source ends, so a run after it reaches nothing. */
TEST(Medium_RegisterProfilerParts, ASessionRemovesTheObserverWhenItsSourceEnds)
{
    RegisteredSource registered;
    const WantedSources wanted;
    ASSERT_TRUE(registered.source->begin(registered.harness.context).ok());
    registered.source->end();

    ASSERT_TRUE(registered.pipeline.graph().run().has_value());
    ASSERT_TRUE(registered.source->begin(registered.harness.context).ok());
    registered.source->collect(K_EVERYTHING, registered.sink);

    EXPECT_EQ(registered.table.size(), 0u);
    EXPECT_EQ(registered.pipeline.graph().compile_count(), 1u);
    registered.source->end();
}

/** @brief Tests that the destructor removes only the observer it added, so another observer keeps its runs. */
TEST(Medium_RegisterProfilerParts, TheDestructorRemovesItsOwnObserverOnly)
{
    SessionPipeline pipeline;
    const auto other = std::make_shared<RecordingObserver>();
    const RunObserverHandle other_handle = pipeline.runtime().add_run_observer(other);
    SushiProfiler::Registry registry;
    SourceHarness harness;
    std::unique_ptr<SushiProfiler::Source> source;
    {
        const RuntimeProfilerParts parts = register_profiler_parts(registry, pipeline.runtime());
        source = make_source(registry);
        ASSERT_TRUE(source->begin(harness.context).ok());
        ASSERT_TRUE(pipeline.graph().run().has_value());
    }

    ASSERT_TRUE(pipeline.graph().run().has_value());

    EXPECT_EQ(other->count(), 2u) << "the other observer heard the run with the parts and the run after them";
    EXPECT_FALSE(other->last().events.empty()) << "device timing stays on while the other observer remains";
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "an observer remained, so no plan was rebuilt";
}

/** @brief Tests that a second runtime may feed the same source when it attaches before a session starts. */
TEST(Medium_RegisterProfilerParts, AFurtherRuntimeAttachesBeforeASession)
{
    SessionPipeline first;
    SessionPipeline second;
    SushiProfiler::Registry registry;
    RuntimeProfilerParts parts = register_profiler_parts(registry, first.runtime());
    SourceHarness harness;

    EXPECT_TRUE(parts.attach(second.runtime()));
    const std::unique_ptr<SushiProfiler::Source> source = make_source(registry);
    ASSERT_TRUE(source->begin(harness.context).ok());
    ASSERT_TRUE(first.graph().run().has_value());
    ASSERT_TRUE(second.graph().run().has_value());

    EXPECT_EQ(first.graph().compile_count(), 1u);
    EXPECT_EQ(second.graph().compile_count(), 1u) << "both runtimes record timing for the session";
    source->end();
}

/** @brief Tests that a further runtime is refused while a session runs. */
TEST(Medium_RegisterProfilerParts, AFurtherRuntimeIsRefusedDuringASession)
{
    SessionPipeline first;
    SessionPipeline third;
    SushiProfiler::Registry registry;
    RuntimeProfilerParts parts = register_profiler_parts(registry, first.runtime());
    SourceHarness harness;
    const std::unique_ptr<SushiProfiler::Source> source = make_source(registry);
    ASSERT_TRUE(source->begin(harness.context).ok());

    EXPECT_FALSE(parts.attach(third.runtime())) << "a session is running";
    source->end();
}

/** @brief Tests that parts whose registration was refused attach nothing. */
TEST(Medium_RegisterProfilerParts, RefusedPartsAttachNothing)
{
    SessionPipeline first;
    SessionPipeline other;
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts accepted = register_profiler_parts(registry, first.runtime());
    RuntimeProfilerParts refused = register_profiler_parts(registry, first.runtime());

    EXPECT_FALSE(refused.attach(other.runtime()));
    EXPECT_TRUE(accepted.is_registered());
}
