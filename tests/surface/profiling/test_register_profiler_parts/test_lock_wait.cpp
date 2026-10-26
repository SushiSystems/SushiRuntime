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
    std::atomic<bool> holding{false};
    std::thread holder([&mutex, &holding]()
    {
        mutex.lock();
        holding.store(true, std::memory_order_release);
        // Held until the waiter's try_lock has failed, so the waiter is certainly contending.
        while (mutex.failed_tries() == 0)
            std::this_thread::yield();
        mutex.unlock();
    });
    while (!holding.load(std::memory_order_acquire))
        std::this_thread::yield();

    lock_and_release(mutex);
    holder.join();

    EXPECT_EQ(mutex.failed_tries(), 1u);
    EXPECT_EQ(waits_written(), 1u) << "one wait, one zone";
}

/** @brief Tests that the guard releases the mutex when its scope ends, so the next lock is uncontended. */
TEST_F(Medium_PendingMutexLockWait, TheGuardReleasesTheMutexAtTheEndOfItsScope)
{
    CountingMutex mutex;

    lock_and_release(mutex);

    EXPECT_TRUE(mutex.try_lock()) << "the scope released it";
    mutex.unlock();
}
