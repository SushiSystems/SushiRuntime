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
 * @file test_run_observer.cpp
 * @brief Tests RunObservation and RunObserverHandle, the way a run's timing leaves the runtime.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/report/run_observer.hpp>

using namespace SushiRuntime;

static_assert(!std::is_copy_constructible_v<RunObserverHandle>, "a handle removes one observer, once");
static_assert(!std::is_copy_assignable_v<RunObserverHandle>, "a handle removes one observer, once");
static_assert(std::is_nothrow_move_constructible_v<RunObserverHandle>, "a handle moves without throwing");

namespace
{
    /** @brief Counts the observations it receives. */
    class CountingObserver final : public RunObserver
    {
        public:
            void on_run(const RunObservation& observation) noexcept override
            {
                ++runs;
                last_events = observation.events.size();
            }

            int runs = 0;                  /**< How many runs were observed. */
            std::size_t last_events = 0;   /**< Records of the latest observation. */
    };
} // namespace

/** @brief Tests that an observation starts empty and on the scheduler layer. */
TEST(Small_RunObservation, StartsEmpty)
{
    const RunObservation observation;

    EXPECT_EQ(observation.layer, ExecutionLayer::Scheduler);
    EXPECT_EQ(observation.table, nullptr);
    EXPECT_TRUE(observation.events.empty());
    EXPECT_TRUE(observation.device_clocks.empty());
    EXPECT_FALSE(observation.drops.any());
}

/** @brief Tests that an observer is called through the interface with the observation it is given. */
TEST(Small_RunObservation, AnObserverReadsTheObservationItIsGiven)
{
    CountingObserver observer;
    RunObservation observation;
    observation.events.resize(3);

    static_cast<RunObserver&>(observer).on_run(observation);

    EXPECT_EQ(observer.runs, 1);
    EXPECT_EQ(observer.last_events, 3u);
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

/** @brief Tests that a moved handle carries the removal and the moved-from one removes nothing. */
TEST(Small_RunObserverHandle, AMovedHandleCarriesTheRemoval)
{
    int removals = 0;
    RunObserverHandle first([&removals]() { ++removals; });

    RunObserverHandle second(std::move(first));
    EXPECT_FALSE(static_cast<bool>(first));
    EXPECT_TRUE(static_cast<bool>(second));
    first.reset();
    EXPECT_EQ(removals, 0);

    RunObserverHandle third;
    third = std::move(second);
    EXPECT_TRUE(static_cast<bool>(third));
    third.reset();
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
