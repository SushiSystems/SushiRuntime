/**************************************************************************/
/* test_profile.cpp                                                       */
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
 * @file test_profile.cpp
 * @brief Tests the SR_ZONE family at the level this build compiles, and the thread helpers of the front.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/events/trace_event.hpp>

#include <SushiRuntime/profile/profile.hpp>

namespace
{
    /** @brief Opens the collector before each test and closes and empties it after. */
    class Small_Profile : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                close_and_empty();
                ASSERT_TRUE(SushiProfiler::Collector::instance().open(SushiProfiler::CollectorSettings{}));
            }

            void TearDown() override
            {
                close_and_empty();
            }

            /** @brief Returns the kinds of the events every thread wrote since the last drain, in the order drained. */
            static std::vector<SushiProfiler::EventKind> kinds_written()
            {
                SushiProfiler::DrainBatch batch;
                SushiProfiler::Collector::instance().drain(batch);
                std::vector<SushiProfiler::EventKind> kinds;
                for (const SushiProfiler::ThreadDrain& thread : batch.threads)
                    for (const SushiProfiler::TraceEvent& event : thread.events)
                        kinds.push_back(SushiProfiler::kind_of(event));
                return kinds;
            }

        private:
            /** @brief Closes the collector and discards what its rings hold. */
            static void close_and_empty()
            {
                SushiProfiler::Collector::instance().close();
                SushiProfiler::DrainBatch discarded;
                SushiProfiler::Collector::instance().drain(discarded);
            }
    };
} // namespace

/** @brief Tests that a stage zone, a value, a counter and an instant each write their own kind at the default level. */
TEST_F(Small_Profile, EachMacroWritesItsKind)
{
    {
        SR_ZONE("profile.test.zone");
    }
    SR_VALUE("profile.test.value", "", 1.5);
    SR_COUNTER("profile.test.counter", "B", Add, 64.0);
    SR_INSTANT("profile.test.instant");

    const std::vector<SushiProfiler::EventKind> kinds = kinds_written();
    EXPECT_EQ(std::count(kinds.begin(), kinds.end(), SushiProfiler::EventKind::Zone), 1);
    EXPECT_EQ(std::count(kinds.begin(), kinds.end(), SushiProfiler::EventKind::Value), 1);
    EXPECT_EQ(std::count(kinds.begin(), kinds.end(), SushiProfiler::EventKind::Counter), 1);
    EXPECT_EQ(std::count(kinds.begin(), kinds.end(), SushiProfiler::EventKind::Instant), 1);
}

/** @brief Tests that a registered thread reads the index the collector gave it. */
TEST_F(Small_Profile, TheThreadIndexIsTheCollectorsOnceTheThreadIsRegistered)
{
    SushiRuntime::Internal::Profile::name_this_thread("profile.test");

    EXPECT_EQ(SushiRuntime::Internal::Profile::current_thread_index(), SushiProfiler::Collector::thread_index());
}

/** @brief Tests that a thread that never recorded reads index 0, which stands for unknown. */
TEST_F(Small_Profile, AThreadThatNeverRecordedReadsZero)
{
    std::uint32_t index = 1;
    std::thread other([&index]() { index = SushiRuntime::Internal::Profile::current_thread_index(); });
    other.join();

    EXPECT_EQ(index, 0u);
}

/** @brief Tests that naming a thread registers it and puts the name in the collector's drain. */
TEST_F(Small_Profile, NamingAThreadPutsTheNameInTheDrain)
{
    std::thread named([]()
    {
        SushiRuntime::Internal::Profile::name_this_thread("profile.named");
        SR_INSTANT("profile.named.tick");
    });
    named.join();

    SushiProfiler::DrainBatch batch;
    SushiProfiler::Collector::instance().drain(batch);
    bool found = false;
    for (const SushiProfiler::ThreadDrain& thread : batch.threads)
        if (thread.name == "profile.named")
            found = true;
    EXPECT_TRUE(found);
}

/** @brief Tests that a lock taken through SR_LOCK_WAIT is released at the end of its scope. */
TEST(Small_ProfileLockWait, TheLockIsReleasedAtTheEndOfItsScope)
{
    std::mutex mutex;
    {
        SR_LOCK_WAIT("profile.test.lock", mutex);
        EXPECT_FALSE(mutex.try_lock()) << "the scope holds the mutex";
    }

    EXPECT_TRUE(mutex.try_lock()) << "the scope released it";
    mutex.unlock();
}
