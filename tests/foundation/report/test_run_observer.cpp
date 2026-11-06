/**************************************************************************/
/* test_run_observer.cpp                                                  */
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
 * @file test_run_observer.cpp
 * @brief Tests RunObservation and RunObserverHandle, the way a run's timing leaves the runtime.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/report/recording_observer.hpp>
#include <SushiRuntime/report/run_observer.hpp>

using namespace SushiRuntime;

static_assert(!std::is_copy_constructible_v<RunObserverHandle>, "a handle removes one observer, once");
static_assert(!std::is_copy_assignable_v<RunObserverHandle>, "a handle removes one observer, once");
static_assert(std::is_nothrow_move_constructible_v<RunObserverHandle>, "a handle moves without throwing");

/** @brief Tests that an observer is called through the interface with the observation it is given. */
TEST(Small_RunObservation, AnObserverReadsTheObservationItIsGiven)
{
    RecordingObserver observer;
    RunObservation observation;
    observation.events.resize(3);

    static_cast<RunObserver&>(observer).on_run(observation);

    ASSERT_EQ(observer.count(), 1u);
    EXPECT_EQ(observer.last().events.size(), 3u);
}

/** @brief Tests that a default handle stands for nothing and a reset of it is harmless. */
TEST(Small_RunObserverHandle, ADefaultHandleStandsForNothing)
{
    RunObserverHandle handle;

    EXPECT_FALSE(static_cast<bool>(handle));
    handle.reset();
    EXPECT_FALSE(static_cast<bool>(handle));
}

/** @brief Tests that reset() removes the observer once and a second call removes nothing. */
TEST(Small_RunObserverHandle, ResetRemovesOnceAndOnlyOnce)
{
    int removals = 0;
    RunObserverHandle handle([&removals]() { ++removals; });

    EXPECT_TRUE(static_cast<bool>(handle));
    handle.reset();
    EXPECT_EQ(removals, 1);
    EXPECT_FALSE(static_cast<bool>(handle));
    handle.reset();
    EXPECT_EQ(removals, 1);
}

/** @brief Tests that destroying a handle removes its observer. */
TEST(Small_RunObserverHandle, DestructionRemovesTheObserver)
{
    int removals = 0;
    {
        const RunObserverHandle handle([&removals]() { ++removals; });
        EXPECT_EQ(removals, 0);
    }
    EXPECT_EQ(removals, 1);
}

/** @brief Tests that move construction hands the removal to the new handle and leaves the old one removing nothing. */
TEST(Small_RunObserverHandle, AMoveConstructedHandleCarriesTheRemoval)
{
    int removals = 0;
    RunObserverHandle first([&removals]() { ++removals; });

    RunObserverHandle second(std::move(first));
    EXPECT_FALSE(static_cast<bool>(first));
    EXPECT_TRUE(static_cast<bool>(second));
    first.reset();
    EXPECT_EQ(removals, 0);
    second.reset();
    EXPECT_EQ(removals, 1);
}

/** @brief Tests that move assignment hands the removal to the target handle. */
TEST(Small_RunObserverHandle, AMoveAssignedHandleCarriesTheRemoval)
{
    int removals = 0;
    RunObserverHandle source([&removals]() { ++removals; });

    RunObserverHandle target;
    target = std::move(source);
    EXPECT_TRUE(static_cast<bool>(target));
    EXPECT_FALSE(static_cast<bool>(source));
    target.reset();
    EXPECT_EQ(removals, 1);
}

/** @brief Tests that assigning over a live handle removes the observer it stood for. */
TEST(Small_RunObserverHandle, AssigningOverALiveHandleRemovesItsObserver)
{
    int first_removals = 0;
    int second_removals = 0;
    RunObserverHandle handle([&first_removals]() { ++first_removals; });

    handle = RunObserverHandle([&second_removals]() { ++second_removals; });

    EXPECT_EQ(first_removals, 1);
    EXPECT_EQ(second_removals, 0);
    handle.reset();
    EXPECT_EQ(second_removals, 1);
}
