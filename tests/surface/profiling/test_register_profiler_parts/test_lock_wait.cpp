/**************************************************************************/
/* test_lock_wait.cpp                                                     */
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
 * @file test_lock_wait.cpp
 * @brief Tests SR_LOCK_WAIT, the front the scheduler locks its submission mutex with.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/events/trace_event.hpp>

#include <SushiRuntime/profile/profile.hpp>

namespace
{
    /** @brief A mutex that counts the try_lock calls that failed, so a test knows when a thread is blocked on it. */
    class CountingMutex
    {
        public:
            bool try_lock()
            {
                if (inner_.try_lock())
                    return true;
                failed_tries_.fetch_add(1, std::memory_order_acq_rel);
                return false;
            }

            void lock() { inner_.lock(); }

            void unlock() { inner_.unlock(); }

            /** @brief Returns how many try_lock calls found the mutex held. */
            std::size_t failed_tries() const { return failed_tries_.load(std::memory_order_acquire); }

        private:
            std::mutex inner_;                           /**< The mutex. */
            std::atomic<std::size_t> failed_tries_{0};   /**< try_lock calls that failed. */
    };

    /** @brief Locks @p mutex for the rest of the call through the front, as the scheduler does. */
    void lock_and_release(CountingMutex& mutex)
    {
        SR_LOCK_WAIT("lock.test", mutex);
    }

    /** @brief Holds a mutex on its own thread until another thread's try_lock on it has failed. */
    class MutexHolder
    {
        public:
            /** @brief Starts the thread and returns once it holds @p mutex. */
            explicit MutexHolder(CountingMutex& mutex)
                : thread_([this, &mutex]() { hold(mutex); })
            {
                wait_until_holding();
            }

            ~MutexHolder()
            {
                thread_.join();
            }

            MutexHolder(const MutexHolder&) = delete;
            MutexHolder& operator=(const MutexHolder&) = delete;

        private:
            /** @brief Locks @p mutex and keeps it until a try_lock has failed on it, so the waiter is certainly contending. */
            void hold(CountingMutex& mutex)
            {
                mutex.lock();
                holding_.store(true, std::memory_order_release);
                while (mutex.failed_tries() == 0)
                    std::this_thread::yield();
                mutex.unlock();
            }

            /** @brief Yields until the thread holds the mutex. */
            void wait_until_holding() const
            {
                while (!holding_.load(std::memory_order_acquire))
                    std::this_thread::yield();
            }

            std::atomic<bool> holding_{false}; /**< Whether the thread holds the mutex. */
            std::thread thread_;               /**< The thread that holds it. */
    };

    /** @brief Opens the collector before each test and closes and empties it after. */
    class Medium_PendingMutexLockWait : public ::testing::Test
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

            /** @brief Returns how many lock-wait zones named lock.test every thread wrote since the last drain. */
            static std::size_t waits_written()
            {
                SushiProfiler::DrainBatch batch;
                SushiProfiler::Collector::instance().drain(batch);
                std::size_t waits = 0;
                for (const SushiProfiler::ThreadDrain& thread : batch.threads)
                    for (const SushiProfiler::TraceEvent& event : thread.events)
                        if (SushiProfiler::Collector::instance().sites().info(SushiProfiler::site_of(event)).name ==
                            "lock.test")
                            ++waits;
                return waits;
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

/** @brief Tests that a lock taken with no one holding it writes no zone. */
TEST_F(Medium_PendingMutexLockWait, AnUncontendedLockWritesNone)
{
    CountingMutex mutex;

    lock_and_release(mutex);
    lock_and_release(mutex);

    EXPECT_EQ(mutex.failed_tries(), 0u);
    EXPECT_EQ(waits_written(), 0u);
}

/** @brief Tests that a lock taken while another thread holds the mutex writes one zone. */
TEST_F(Medium_PendingMutexLockWait, AContendedLockWritesAZone)
{
    CountingMutex mutex;
    const MutexHolder holder(mutex);

    lock_and_release(mutex);

    EXPECT_EQ(mutex.failed_tries(), 1u);
    EXPECT_EQ(waits_written(), 1u) << "one wait, one zone";
}

/** @brief Tests that the guard releases the mutex when its scope ends, so the next lock is uncontended. */
TEST(Small_PendingMutexLockWait, TheGuardReleasesTheMutexAtTheEndOfItsScope)
{
    CountingMutex mutex;

    lock_and_release(mutex);

    EXPECT_TRUE(mutex.try_lock()) << "the scope released it";
    mutex.unlock();
}
