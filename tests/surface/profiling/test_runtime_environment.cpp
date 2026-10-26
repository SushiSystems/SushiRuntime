/**************************************************************************/
/* test_runtime_environment.cpp                                           */
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
 * @file test_runtime_environment.cpp
 * @brief Tests the facts the runtime states about its machine, its devices and its build.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/environment/environment_contributor.hpp>
#include <SushiProfiler/environment/record_facts.hpp>
#include <SushiProfiler/registry/registry.hpp>

#include <SushiRuntime/profiling/register_profiler_parts.hpp>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Returns the facts a registry's contributors state, as a map from name to value. */
    std::map<std::string, std::string> facts_of(const std::vector<SushiProfiler::Fact>& facts)
    {
        std::map<std::string, std::string> map;
        for (const SushiProfiler::Fact& fact : facts)
            map[fact.name] = fact.value;
        return map;
    }
} // namespace

/** @brief Tests that the record states every device the runtime drives with its class, backend and driver. */
TEST(Medium_RuntimeEnvironment, StatesEveryDeviceOfTheRuntime)
{
    auto runtime = Runtime::create().value();
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime);
    ASSERT_TRUE(parts.is_registered());

    const SushiProfiler::RecordFacts facts = SushiProfiler::gather_facts(registry.contributors());

    const auto environment = facts_of(facts.environment);
    const std::string prefix = "sushiruntime." + runtime.topology().name + ".";
    const auto& devices = runtime.advanced().devices();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(environment.at(prefix + "devices"), std::to_string(devices.size()));
    for (const DeviceDescription& device : devices)
    {
        const std::string key = prefix + "device." + std::to_string(device.index) + ".";
        EXPECT_EQ(environment.at(key + "name"), device.name);
        EXPECT_FALSE(environment.at(key + "kind").empty());
        EXPECT_FALSE(environment.at(key + "backend").empty());
        EXPECT_FALSE(environment.at(key + "driver").empty()) << "a driver that is not answerable reads \"unknown\"";
        EXPECT_EQ(environment.at(key + "memory_bytes"), std::to_string(device.memory_bytes));
        EXPECT_EQ(environment.at(key + "compute_units"), std::to_string(device.compute_units));
    }
}

/** @brief Tests that the runtime states its version, its workers and the rebalancer. */
TEST(Medium_RuntimeEnvironment, StatesTheVersionAndTheWorkers)
{
    auto runtime = Runtime::create().value();
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime);

    const auto environment = facts_of(SushiProfiler::gather_facts(registry.contributors()).environment);

    EXPECT_EQ(environment.at("sushiruntime.version"), SUSHIRUNTIME_VERSION_STRING);
    const std::string prefix = "sushiruntime." + runtime.topology().name + ".";
    EXPECT_EQ(environment.at(prefix + "workers"), std::to_string(runtime.advanced().worker_pool().count));
    EXPECT_EQ(environment.at(prefix + "rebalancer"), "off");
}

/** @brief Tests that the library's own build type is a build fact, so two records of different builds do not compare. */
TEST(Medium_RuntimeEnvironment, StatesTheBuildTypeAsABuildFact)
{
    auto runtime = Runtime::create().value();
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime);

    const auto build = facts_of(SushiProfiler::gather_facts(registry.contributors()).build);

    const std::string build_type = build.at("sushiruntime.build_type");
    EXPECT_TRUE(build_type == "release" || build_type == "debug") << build_type;
    EXPECT_FALSE(build.at("sushiruntime.profile_level").empty());
    EXPECT_FALSE(build.at("sushiruntime.sanitizer").empty());
    EXPECT_FALSE(build.at("sushiruntime.sycl_toolchain").empty());
    EXPECT_EQ(facts_of(SushiProfiler::gather_facts(registry.contributors()).environment).count("sushiruntime.build_type"), 0u)
        << "a build fact is not an environment fact";
}

/** @brief Tests that a registry that refused the source states nothing of the runtime. */
TEST(Medium_RuntimeEnvironment, ARefusedRegistrationStatesNothingTwice)
{
    auto runtime = Runtime::create().value();
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts first = register_profiler_parts(registry, runtime);
    const RuntimeProfilerParts second = register_profiler_parts(registry, runtime);

    EXPECT_TRUE(first.is_registered());
    EXPECT_FALSE(second.is_registered());
    EXPECT_EQ(registry.contributors().size(), 1u) << "the refused parts added no contributor";
}
