/**************************************************************************/
/* test_run_observer_registry.cpp                                         */
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
 * @file test_run_observer_registry.cpp
 * @brief Tests the gate and delivery rules of RunObserverRegistry on recording observers.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/execution/run_observer_registry.hpp>
#include <SushiRuntime/report/recording_observer.hpp>

using SushiRuntime::RecordingObserver;
using SushiRuntime::RunObservation;
using SushiRuntime::Internal::Execution::RunObserverRegistry;

namespace
{
    /** @brief A gate that is open. */
    bool open_gate() noexcept
    {
        return true;
    }

    /** @brief A gate that is closed. */
    bool closed_gate() noexcept
    {
        return false;
    }
} // namespace

/** @brief Tests that a registry with no observer wants no device timing. */
TEST(Small_RunObserverRegistry, AnEmptyRegistryWantsNoTiming)
{
    const RunObserverRegistry registry;
    EXPECT_FALSE(registry.wants_device_timing());
}

/** @brief Tests that an observer with no gate wants timing until it is removed. */
TEST(Small_RunObserverRegistry, AnUngatedObserverWantsTimingUntilItIsRemoved)
{
    RunObserverRegistry registry;

    const std::uint64_t id = registry.add(std::make_shared<RecordingObserver>(), nullptr);
    EXPECT_TRUE(registry.wants_device_timing());

    registry.remove(id);
    EXPECT_FALSE(registry.wants_device_timing());
}

/** @brief Tests that an observer behind an open gate wants timing. */
TEST(Small_RunObserverRegistry, AnOpenGateWantsTiming)
{
    RunObserverRegistry registry;
    registry.add(std::make_shared<RecordingObserver>(), &open_gate);
    EXPECT_TRUE(registry.wants_device_timing());
}

/** @brief Tests that an observer behind a closed gate costs no device timing. */
TEST(Small_RunObserverRegistry, AClosedGateWantsNoTiming)
{
    RunObserverRegistry registry;
    registry.add(std::make_shared<RecordingObserver>(), &closed_gate);
    EXPECT_FALSE(registry.wants_device_timing());
}

/** @brief Tests that an ungated observer keeps timing on whatever the gate of another observer says. */
TEST(Small_RunObserverRegistry, AnUngatedObserverKeepsTimingOnWhateverAnotherGateSays)
{
    RunObserverRegistry registry;
    registry.add(std::make_shared<RecordingObserver>(), &closed_gate);
    registry.add(std::make_shared<RecordingObserver>(), nullptr);
    EXPECT_TRUE(registry.wants_device_timing());
}

/** @brief Tests that a delivery reaches an observer whose gate is open or absent. */
TEST(Small_RunObserverRegistry, ADeliveryReachesAnObserverWhoseGateIsOpenOrAbsent)
{
    RunObserverRegistry registry;
    const auto gated = std::make_shared<RecordingObserver>();
    const auto ungated = std::make_shared<RecordingObserver>();
    registry.add(gated, &open_gate);
    registry.add(ungated, nullptr);

    registry.notify(RunObservation{});

    EXPECT_EQ(gated->count(), 1u);
    EXPECT_EQ(ungated->count(), 1u);
}

/** @brief Tests that a delivery skips an observer whose gate is closed. */
TEST(Small_RunObserverRegistry, ADeliverySkipsAnObserverWhoseGateIsClosed)
{
    RunObserverRegistry registry;
    const auto gated = std::make_shared<RecordingObserver>();
    registry.add(gated, &closed_gate);

    registry.notify(RunObservation{});

    EXPECT_EQ(gated->count(), 0u);
}

/** @brief Tests that a removed observer hears nothing more while the others still do. */
TEST(Small_RunObserverRegistry, ARemovedObserverHearsNothingMore)
{
    RunObserverRegistry registry;
    const auto removed = std::make_shared<RecordingObserver>();
    const auto kept = std::make_shared<RecordingObserver>();
    const std::uint64_t removed_id = registry.add(removed, nullptr);
    registry.add(kept, nullptr);

    registry.remove(removed_id);
    registry.notify(RunObservation{});

    EXPECT_EQ(removed->count(), 0u);
    EXPECT_EQ(kept->count(), 1u);
}

/** @brief Tests that removing an id nobody holds leaves the observers added. */
TEST(Small_RunObserverRegistry, RemovingAnUnknownIdLeavesTheObserversAdded)
{
    RunObserverRegistry registry;
    const std::uint64_t id = registry.add(std::make_shared<RecordingObserver>(), nullptr);

    registry.remove(id + 1);

    EXPECT_TRUE(registry.wants_device_timing());
}

/** @brief Tests that clear() removes every observer. */
TEST(Small_RunObserverRegistry, ClearRemovesEveryObserver)
{
    RunObserverRegistry registry;
    const auto observer = std::make_shared<RecordingObserver>();
    registry.add(observer, nullptr);

    registry.clear();
    registry.notify(RunObservation{});

    EXPECT_FALSE(registry.wants_device_timing());
    EXPECT_EQ(observer->count(), 0u);
}
