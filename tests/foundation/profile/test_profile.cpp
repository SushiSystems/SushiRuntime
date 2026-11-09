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
 * @file test_profile.cpp
 * @brief Tests the SR_ZONE family at the level this build compiles, and the thread helpers of the front.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
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
    class ProfileTest : public ::testing::Test
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

            /** @brief Returns the drain of the collector since the last drain. */
            static SushiProfiler::DrainBatch drained()
            {
                SushiProfiler::DrainBatch batch;
                SushiProfiler::Collector::instance().drain(batch);
                return batch;
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

    /** @brief The profile tests that touch no thread besides the test's own. */
    class Small_Profile : public ProfileTest
    {
    };

    /** @brief The profile tests that start a thread. */
    class Medium_Profile : public ProfileTest
    {
    };

    /** @brief Returns the names of the threads in @p batch. */
    std::vector<std::string> thread_names(const SushiProfiler::DrainBatch& batch)
    {
        std::vector<std::string> names;
        for (const SushiProfiler::ThreadDrain& thread : batch.threads)
            names.push_back(thread.name);
        return names;
    }

    /** @brief Returns the drained thread named @p name, or null when the batch holds none. */
    const SushiProfiler::ThreadDrain* thread_named(const SushiProfiler::DrainBatch& batch, const std::string& name)
    {
        for (const SushiProfiler::ThreadDrain& thread : batch.threads)
            if (thread.name == name)
                return &thread;
        return nullptr;
    }
} // namespace

/** @brief Tests that a stage zone writes one event of the zone kind at the default level. */
TEST_F(Small_Profile, AZoneWritesAZoneEvent)
{
    {
        SR_ZONE(::SushiRuntime::Internal::Profile::process_collector(), "profile.test.zone");
    }

    EXPECT_EQ(kinds_written(), (std::vector<SushiProfiler::EventKind>{SushiProfiler::EventKind::Zone}));
}

/** @brief Tests that a value writes one event of the value kind. */
TEST_F(Small_Profile, AValueWritesAValueEvent)
{
    SR_VALUE(::SushiRuntime::Internal::Profile::process_collector(), "profile.test.value", "", 1.5);

    EXPECT_EQ(kinds_written(), (std::vector<SushiProfiler::EventKind>{SushiProfiler::EventKind::Value}));
}

/** @brief Tests that a counter writes one event of the counter kind. */
TEST_F(Small_Profile, ACounterWritesACounterEvent)
{
    SR_COUNTER(::SushiRuntime::Internal::Profile::process_collector(), "profile.test.counter", "B", Add, 64.0);

    EXPECT_EQ(kinds_written(), (std::vector<SushiProfiler::EventKind>{SushiProfiler::EventKind::Counter}));
}

/** @brief Tests that an instant writes one event of the instant kind. */
TEST_F(Small_Profile, AnInstantWritesAnInstantEvent)
{
    SR_INSTANT("profile.test.instant");

    EXPECT_EQ(kinds_written(), (std::vector<SushiProfiler::EventKind>{SushiProfiler::EventKind::Instant}));
}

/** @brief Tests that a registered thread reads the index its drained entry carries. */
TEST_F(Small_Profile, TheThreadIndexIsTheOneItsDrainedEntryCarries)
{
    SushiRuntime::Internal::Profile::name_this_thread("profile.test");
    SR_INSTANT("profile.test.tick");

    const SushiProfiler::DrainBatch batch = drained();
    const SushiProfiler::ThreadDrain* entry = thread_named(batch, "profile.test");

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(SushiRuntime::Internal::Profile::current_thread_index(), entry->thread_index);
}

/** @brief Tests that naming a thread registers it and puts the name in the collector's drain. */
TEST_F(Medium_Profile, NamingAThreadPutsTheNameInTheDrain)
{
    std::thread named([]()
    {
        SushiRuntime::Internal::Profile::name_this_thread("profile.named");
        SR_INSTANT("profile.named.tick");
    });
    named.join();

    const std::vector<std::string> names = thread_names(drained());

    EXPECT_EQ(std::count(names.begin(), names.end(), "profile.named"), 1);
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
