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

#include "support/runtime_environment.hpp"

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

    /** @brief Expects @p environment to state the name, class, backend, driver, memory and units of @p device under @p prefix. */
    void expect_device_stated(const std::map<std::string, std::string>& environment, const std::string& prefix,
                              const DeviceDescription& device)
    {
        const std::string key = prefix + "device." + std::to_string(device.index) + ".";
        EXPECT_EQ(environment.at(key + "name"), device.name);
        EXPECT_FALSE(environment.at(key + "kind").empty());
        EXPECT_FALSE(environment.at(key + "backend").empty());
        EXPECT_FALSE(environment.at(key + "driver").empty()) << "a driver that is not answerable reads \"unknown\"";
        EXPECT_EQ(environment.at(key + "memory_bytes"), std::to_string(device.memory_bytes));
        EXPECT_EQ(environment.at(key + "compute_units"), std::to_string(device.compute_units));
    }

    /** @brief Expects every device of @p devices to be stated in @p environment under @p prefix. */
    template <class Devices>
    void expect_every_device_stated(const std::map<std::string, std::string>& environment, const std::string& prefix,
                                    const Devices& devices)
    {
        for (const DeviceDescription& device : devices)
            expect_device_stated(environment, prefix, device);
    }

    /** @brief Fixture of the tests that read the facts the runtime of the program states. */
    class Medium_RuntimeEnvironment : public TestSupport::RuntimeTest
    {
    };
} // namespace

/** @brief Tests that the record states how many devices the runtime drives. */
TEST_F(Medium_RuntimeEnvironment, StatesTheNumberOfDevices)
{
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime());
    ASSERT_TRUE(parts.is_registered());

    const auto environment = facts_of(SushiProfiler::gather_facts(registry.contributors()).environment);

    const std::string prefix = "sushiruntime." + runtime().topology().name + ".";
    EXPECT_EQ(environment.at(prefix + "devices"), std::to_string(runtime().advanced().devices().size()));
}

/** @brief Tests that the record states every device the runtime drives with its class, backend and driver. */
TEST_F(Medium_RuntimeEnvironment, StatesEveryDeviceOfTheRuntime)
{
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime());
    ASSERT_TRUE(parts.is_registered());

    const auto environment = facts_of(SushiProfiler::gather_facts(registry.contributors()).environment);

    const auto& devices = runtime().advanced().devices();
    ASSERT_FALSE(devices.empty());
    expect_every_device_stated(environment, "sushiruntime." + runtime().topology().name + ".", devices);
}

/** @brief Tests that the runtime states its version, its workers and the rebalancer. */
TEST_F(Medium_RuntimeEnvironment, StatesTheVersionAndTheWorkers)
{
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime());

    const auto environment = facts_of(SushiProfiler::gather_facts(registry.contributors()).environment);

    EXPECT_EQ(environment.at("sushiruntime.version"), SUSHIRUNTIME_VERSION_STRING);
    const std::string prefix = "sushiruntime." + runtime().topology().name + ".";
    EXPECT_EQ(environment.at(prefix + "workers"), std::to_string(runtime().advanced().worker_pool().count));
    EXPECT_EQ(environment.at(prefix + "rebalancer"), "off");
}

/** @brief Tests that the library's own build type is a build fact, so two records of different builds do not compare. */
TEST_F(Medium_RuntimeEnvironment, StatesTheBuildTypeAsABuildFact)
{
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime());

    const auto build = facts_of(SushiProfiler::gather_facts(registry.contributors()).build);

    const std::string build_type = build.at("sushiruntime.build_type");
    EXPECT_TRUE(build_type == "release" || build_type == "debug") << build_type;
    EXPECT_FALSE(build.at("sushiruntime.profile_level").empty());
    EXPECT_FALSE(build.at("sushiruntime.sanitizer").empty());
    EXPECT_FALSE(build.at("sushiruntime.sycl_toolchain").empty());
}

/** @brief Tests that a build fact is not stated as an environment fact. */
TEST_F(Medium_RuntimeEnvironment, ABuildFactIsNotAnEnvironmentFact)
{
    SushiProfiler::Registry registry;
    const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime());

    const auto environment = facts_of(SushiProfiler::gather_facts(registry.contributors()).environment);

    EXPECT_EQ(environment.count("sushiruntime.build_type"), 0u);
}
