/**************************************************************************/
/* test_collector_lock.cpp                                                */
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
 * @file test_collector_lock.cpp
 * @brief Tests the guard behind SR_LOCK_WAIT against a mutex that reports itself held or free.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <mutex>

#include <gtest/gtest.h>

#include <SushiProfiler/clock/manual_tick_source.hpp>
#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/events/trace_event.hpp>

#include <SushiRuntime/profile/profile.hpp>

namespace
{
    /** @brief A mutex whose first try_lock calls fail as many times as it was told, then lock like a plain one. */
    class BusyMutex
    {
        public:
            /** @brief Makes a mutex that refuses @p refusals try_lock calls before it grants one. */
            explicit BusyMutex(std::size_t refusals) : refusals_(refusals)
            {
            }

            bool try_lock()
            {
                if (refusals_ > 0)
                {
                    --refusals_;
                    return false;
                }
                return inner_.try_lock();
            }

            void lock() { inner_.lock(); }

            void unlock() { inner_.unlock(); }

            /** @brief Returns whether the mutex is held now. */
            bool is_held()
            {
                if (!inner_.try_lock())
                    return true;
                inner_.unlock();
                return false;
            }

        private:
            std::size_t refusals_;  /**< How many try_lock calls still fail. */
            std::mutex inner_;      /**< The mutex that lock() takes. */
    };

    /** @brief Owns a collector on a tick source that never moves. */
    class CollectorLockTest : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                ASSERT_TRUE(collector_.open(SushiProfiler::CollectorSettings{}));
            }

            void TearDown() override
            {
                collector_.close();
            }

            /** @brief Returns the collector the locks record into. */
            SushiRuntime::Internal::Profile::Collector& collector() { return collector_; }

            /** @brief Returns how many events the collector holds since the last drain. */
            std::size_t events_written()
            {
                SushiProfiler::DrainBatch batch;
                collector_.drain(batch);
                std::size_t events = 0;
                for (const SushiProfiler::ThreadDrain& thread : batch.threads)
                    events += thread.events.size();
                return events;
            }

        private:
            SushiProfiler::ManualTickSource ticks_;      /**< The clock the collector stamps with. */
            SushiProfiler::Collector collector_{ticks_}; /**< The collector the locks record into. */
    };

    /** @brief The tests of the guard; none starts a thread. */
    class Small_CollectorLock : public CollectorLockTest
    {
    };
} // namespace

/** @brief Tests that a lock taken through SR_LOCK_WAIT is released at the end of its scope. */
TEST_F(Small_CollectorLock, TheLockIsReleasedAtTheEndOfItsScope)
{
    BusyMutex mutex(0);
    {
        SR_LOCK_WAIT(collector(), "profile.test.lock", mutex);
        EXPECT_TRUE(mutex.is_held()) << "the scope holds the mutex";
    }

    EXPECT_FALSE(mutex.is_held()) << "the scope released it";
}

/** @brief Tests that a lock taken at the first try_lock writes no event. */
TEST_F(Small_CollectorLock, AFreeMutexWritesNothing)
{
    BusyMutex mutex(0);
    {
        SR_LOCK_WAIT(collector(), "profile.test.free", mutex);
    }

    EXPECT_EQ(events_written(), 0U);
}

#if SR_ENABLE_PROFILER

/** @brief Tests that a lock whose try_lock failed still locks, and writes one wait event. */
TEST_F(Small_CollectorLock, ABusyMutexWritesOneWaitAndStillLocks)
{
    if (SushiRuntime::Internal::Profile::K_COMPILED_LEVEL < 1)
        GTEST_SKIP() << "The lock wait zone is compiled in at level 1.";
    BusyMutex mutex(1);
    {
        SR_LOCK_WAIT(collector(), "profile.test.busy", mutex);
        EXPECT_TRUE(mutex.is_held());
    }

    EXPECT_EQ(events_written(), 1U);
}

#endif // SR_ENABLE_PROFILER
