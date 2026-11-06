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
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/environment/environment_contributor.hpp>
#include <SushiProfiler/environment/record_facts.hpp>
#include <SushiProfiler/registry/registry.hpp>

#include <SushiRuntime/profiling/in_memory_profiled_runtime.hpp>
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

    /** @brief Registers @p runtime with a fresh registry and returns the facts its contributors state. */
    SushiProfiler::RecordFacts facts_stated_by(const std::shared_ptr<ProfiledRuntime>& runtime)
    {
        SushiProfiler::Registry registry;
        const RuntimeProfilerParts parts = register_profiler_parts(registry, runtime);
        EXPECT_TRUE(parts.is_registered());
        return SushiProfiler::gather_facts(registry.contributors());
    }

    /** @brief One state of the rebalancer and how the record spells it. */
    struct RebalancerRow
    {
        bool enabled;        /**< Whether the runtime reports the rebalancer on. */
        const char* stated;  /**< The spelling of the fact. */
    };

    /** @brief Fixture of the tests that read the facts the runtime of the program states. */
    class Medium_RuntimeEnvironment : public TestSupport::RuntimeTest
    {
    };

    /** @brief Fixture of the tests that state the facts of a runtime held as plain values. */
    class Small_RuntimeEnvironmentRebalancer : public ::testing::TestWithParam<RebalancerRow>
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

/** @brief Tests that the runtime states its version and its worker count under its node's name. */
TEST(Small_RuntimeEnvironmentFacts, StatesTheVersionAndTheWorkers)
{
    auto runtime = std::make_shared<InMemoryProfiledRuntime>("alpha");
    runtime->set_workers(6, false);

    const auto environment = facts_of(facts_stated_by(runtime).environment);

    EXPECT_EQ(environment.at("sushiruntime.version"), SUSHIRUNTIME_VERSION_STRING);
    EXPECT_EQ(environment.at("sushiruntime.alpha.workers"), "6");
}

/** @brief Tests that the runtime spells the state of its rebalancer on or off. */
TEST_P(Small_RuntimeEnvironmentRebalancer, StatesTheRebalancer)
{
    auto runtime = std::make_shared<InMemoryProfiledRuntime>("alpha");
    runtime->set_workers(1, GetParam().enabled);

    const auto environment = facts_of(facts_stated_by(runtime).environment);

    EXPECT_EQ(environment.at("sushiruntime.alpha.rebalancer"), GetParam().stated);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_RuntimeEnvironmentRebalancer,
                         ::testing::Values(RebalancerRow{false, "off"}, RebalancerRow{true, "on"}));

/** @brief Tests that a device the runtime reports is stated with its description and its driver. */
TEST(Small_RuntimeEnvironmentFacts, StatesADeviceWithItsDriver)
{
    auto runtime = std::make_shared<InMemoryProfiledRuntime>("alpha");
    DeviceDescription device;
    device.index = 0;
    device.name = "Test GPU";
    device.kind = DeviceKind::GPU;
    device.memory_bytes = 8192;
    device.compute_units = 12;
    runtime->add_device(device, "1.2.3");

    const auto environment = facts_of(facts_stated_by(runtime).environment);

    EXPECT_EQ(environment.at("sushiruntime.alpha.devices"), "1");
    EXPECT_EQ(environment.at("sushiruntime.alpha.device.0.name"), "Test GPU");
    EXPECT_EQ(environment.at("sushiruntime.alpha.device.0.kind"), "gpu");
    EXPECT_EQ(environment.at("sushiruntime.alpha.device.0.driver"), "1.2.3");
    EXPECT_EQ(environment.at("sushiruntime.alpha.device.0.memory_bytes"), "8192");
    EXPECT_EQ(environment.at("sushiruntime.alpha.device.0.compute_units"), "12");
}

/** @brief Tests that the library's own build type is a build fact, so two records of different builds do not compare. */
TEST(Small_RuntimeEnvironmentFacts, StatesTheBuildTypeAsABuildFact)
{
    const auto build = facts_of(facts_stated_by(std::make_shared<InMemoryProfiledRuntime>()).build);

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
