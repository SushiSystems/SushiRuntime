/**************************************************************************/
/* test_profiled_runtime.cpp                                              */
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
 * @file test_profiled_runtime.cpp
 * @brief Tests the ProfiledRuntime contract on the in-memory adapter and on the adapter over a Runtime.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/in_memory_profiled_runtime.hpp>
#include <SushiRuntime/profiling/profiled_runtime.hpp>
#include <SushiRuntime/report/recording_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Expects every device of @p runtime to sit at its own index and to name a driver. */
    void expect_devices_indexed_by_position(const ProfiledRuntime& runtime)
    {
        const std::vector<DeviceDescription> devices = runtime.devices();
        for (std::size_t position = 0; position < devices.size(); ++position)
        {
            EXPECT_EQ(devices[position].index, position);
            EXPECT_FALSE(runtime.driver_of(position).empty()) << "a driver that does not answer reads \"unknown\"";
        }
    }

    /** @brief Expects an observer added to @p runtime to be stood for by a handle that a reset empties. */
    void expect_a_handle_stands_for_its_observer(ProfiledRuntime& runtime)
    {
        RunObserverHandle handle = runtime.observe_runs(std::make_shared<RecordingObserver>(), nullptr);

        EXPECT_TRUE(static_cast<bool>(handle));
        handle.reset();
        EXPECT_FALSE(static_cast<bool>(handle));
    }

    /** @brief Returns a runtime named "alpha" with a CPU at index 0 and a GPU at index 1. */
    std::shared_ptr<InMemoryProfiledRuntime> two_device_runtime()
    {
        auto runtime = std::make_shared<InMemoryProfiledRuntime>("alpha");
        DeviceDescription cpu;
        cpu.index = 0;
        cpu.name = "cpu";
        DeviceDescription gpu;
        gpu.index = 1;
        gpu.name = "gpu";
        runtime->add_device(cpu, "1.0");
        runtime->add_device(gpu, "2.0");
        return runtime;
    }

    /** @brief Fixture of the contract tests over a Runtime. */
    class Small_ContextProfiledRuntime : public TestSupport::HostRuntimeTest
    {
    };
} // namespace

/** @brief Tests that the in-memory runtime keeps its devices at their indices with their drivers. */
TEST(Small_InMemoryProfiledRuntime, KeepsEachDeviceAtItsIndexWithItsDriver)
{
    const auto runtime = two_device_runtime();

    expect_devices_indexed_by_position(*runtime);
    EXPECT_EQ(runtime->driver_of(1), "2.0");
}

/** @brief Tests that a driver nobody set reads "unknown". */
TEST(Small_InMemoryProfiledRuntime, ADeviceWithNoStoredDriverReadsUnknown)
{
    const InMemoryProfiledRuntime runtime;

    EXPECT_EQ(runtime.driver_of(7), "unknown");
}

/** @brief Tests that a reset handle takes its observer out and leaves the others. */
TEST(Small_InMemoryProfiledRuntime, AResetHandleRemovesOnlyItsObserver)
{
    InMemoryProfiledRuntime runtime;
    RunObserverHandle first = runtime.observe_runs(std::make_shared<RecordingObserver>(), nullptr);
    const RunObserverHandle second = runtime.observe_runs(std::make_shared<RecordingObserver>(), nullptr);
    ASSERT_EQ(runtime.observer_count(), 2u);

    first.reset();

    EXPECT_EQ(runtime.observer_count(), 1u);
}

/** @brief Tests that the in-memory runtime hands out a handle that stands for its observer. */
TEST(Small_InMemoryProfiledRuntime, AnObserverIsStoodForByAHandle)
{
    InMemoryProfiledRuntime runtime;

    expect_a_handle_stands_for_its_observer(runtime);
}

/** @brief Tests that the runtime adapter keeps its devices at their indices with a driver each. */
TEST_F(Small_ContextProfiledRuntime, KeepsEachDeviceAtItsIndexWithItsDriver)
{
    const std::shared_ptr<ProfiledRuntime> profiled = profiled_runtime_of(runtime());

    expect_devices_indexed_by_position(*profiled);
}

/** @brief Tests that the runtime adapter states the node, the devices and the workers the runtime states. */
TEST_F(Small_ContextProfiledRuntime, StatesWhatTheRuntimeStates)
{
    const std::shared_ptr<ProfiledRuntime> profiled = profiled_runtime_of(runtime());

    EXPECT_EQ(profiled->node_name(), runtime().topology().name);
    EXPECT_EQ(profiled->devices().size(), runtime().advanced().devices().size());
    EXPECT_EQ(profiled->worker_count(), runtime().advanced().worker_pool().count);
}

/** @brief Tests that the runtime adapter hands out a handle that stands for its observer. */
TEST_F(Small_ContextProfiledRuntime, AnObserverIsStoodForByAHandle)
{
    const std::shared_ptr<ProfiledRuntime> profiled = profiled_runtime_of(runtime());

    expect_a_handle_stands_for_its_observer(*profiled);
}
