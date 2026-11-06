/**************************************************************************/
/* test_epoch_reclamation.cpp                                             */
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
 * @file test_epoch_reclamation.cpp
 * @brief Tests that epoch reclamation frees retired objects once and adopts dead threads' orphans.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>

using namespace SushiRuntime::Internal::Memory;

namespace
{
    using Counter = std::shared_ptr<std::atomic<int>>;

    /**
     * @brief Helper to create a shared atomic counter.
     *
     * @return A shared_ptr to a new atomic counter initialized to 0.
     */
    inline Counter make_counter() { return std::make_shared<std::atomic<int>>(0); }

    /** @brief Holds a heap payload that bumps a shared counter on destruction. */
    struct Tracked
    {
        Counter destroyed;
        explicit Tracked(Counter d) : destroyed(std::move(d)) {}
        ~Tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
    };

    /**
     * @brief Retires a count of objects, runs extra guarded passes and returns the number
     *        reclaimed.
     */
    int retire_and_drain(const Counter& destroyed, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        for (int i = 0; i < 512; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        return destroyed->load(std::memory_order_relaxed);
    }
}

/**
 * @brief Tests that constructing and destroying an EpochGuard is safe and balanced.
 *
 * Checks that nested guards do not throw exceptions or leave unbalanced state.
 */
TEST(Medium_EpochReclamationTest, GuardEnterExitBalanced)
{
    EXPECT_NO_THROW({
        EpochGuard outer;
        {
            EpochGuard inner;
        }
    });
}

/**
 * @brief Tests that the bulk of retired objects are deleted after enough guarded retire calls.
 *
 * Verifies that the reclamation process eventually frees objects from past epochs.
 */
TEST(Medium_EpochReclamationTest, RetiredObjectsEventuallyReclaimed)
{
    Counter destroyed = make_counter();
    constexpr int RETIRED = 1024;

    int reclaimed = retire_and_drain(destroyed, RETIRED);

    // Objects retired in the last epoch may survive, so the bound is loose; see README.md.
    EXPECT_GT(reclaimed, RETIRED) << "Epoch reclamation did not collect retired objects.";
}

/**
 * @brief Tests that a thread that retires then exits hands off its orphaned nodes.
 *
 * Its nodes go to the global orphan list and are reclaimed by another thread's draining work.
 */
TEST(Medium_EpochReclamationTest, ThreadExitHandsOffOrphans)
{
    Counter destroyed = make_counter();
    constexpr int RETIRED = 300;

    std::thread producer([destroyed]()
    {
        for (int i = 0; i < RETIRED; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        // The exiting thread's destructor gives its retire list to the orphan list; see README.md.
    });
    producer.join();

    // Each guarded retire adopts orphans and frees those past the safe epoch; see README.md.
    Counter drain_sink = make_counter();
    for (int i = 0; i < 1024; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(drain_sink));
    }

    EXPECT_GT(destroyed->load(), 0)
        << "Orphaned nodes from an exited thread were never reclaimed (leak).";
}

/**
 * @brief Tests that many threads retiring at once do not corrupt the manager.
 *
 * Ensures concurrent retirements are thread-safe and objects are eventually reclaimed.
 */
TEST(Medium_EpochReclamationTest, ConcurrentRetireNoCrash)
{
    Counter destroyed = make_counter();
    constexpr int THREADS    = 8;
    constexpr int PER_THREAD = 500;

    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
    {
        workers.emplace_back([destroyed]()
        {
            for (int i = 0; i < PER_THREAD; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new Tracked(destroyed));
            }
        });
    }
    for (auto& w : workers)
        w.join();

    EXPECT_GT(destroyed->load(), 0) << "No objects reclaimed under concurrent retire.";
}

namespace
{
    /// @brief Number of thread slots EpochManager holds; see README.md.
    constexpr std::size_t K_EPOCH_SLOTS = SushiRuntime::K_MAX_WORKER_THREADS + 32;

    /** @brief Parks threads after they report, until the test lets them exit. */
    class ThreadPark
    {
        public:
            /** @brief Counts the calling thread as reported and blocks it until release(). */
            void report_and_wait()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ++reported_;
                changed_.notify_all();
                changed_.wait(lock, [this] { return released_; });
            }

            /** @brief Blocks until @p count threads have reported. */
            void wait_for_reports(std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [this, count] { return reported_ >= count; });
            }

            /** @brief Lets every parked thread return. */
            void release()
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                released_ = true;
                changed_.notify_all();
            }

        private:
            /// @brief Guards the two fields below.
            std::mutex mutex_;
            /// @brief Signals a change of either field.
            std::condition_variable changed_;
            /// @brief Number of threads that have reported.
            std::size_t reported_ = 0;
            /// @brief Whether parked threads may return.
            bool released_ = false;
    };
}

namespace
{
    /** @brief Parks one thread more than there are slots, each holding a guard's slot. */
    void hold_one_slot_too_many()
    {
        constexpr std::size_t THREADS = K_EPOCH_SLOTS + 1;
        ThreadPark park;

        std::vector<std::thread> threads;
        threads.reserve(THREADS);
        for (std::size_t i = 0; i < THREADS; ++i)
        {
            threads.emplace_back([&park]
            {
                {
                    EpochGuard guard;
                }
                // A registered thread keeps its slot until it exits; see README.md.
                park.report_and_wait();
            });
        }

        park.wait_for_reports(THREADS);
        park.release();
        for (std::thread& thread : threads)
            thread.join();
    }
} // namespace

/**
 * @brief Tests that a thread arriving when every slot is held stops the process, naming the limit.
 */
TEST(Medium_EpochReclamationDeathTest, AThreadPastTheSlotLimitStopsNamingTheLimit)
{
    EXPECT_DEATH(hold_one_slot_too_many(), std::to_string(K_EPOCH_SLOTS) + " epoch slots");

    std::thread late([] { EXPECT_NO_THROW({ EpochGuard guard; }); });
    late.join();
}

/** @brief Tests that a reader that blocks reclamation costs one scan per K_RETIRE_THRESHOLD. */
TEST(Medium_EpochReclamationTest, ABlockedReclaimScansOncePerThreshold)
{
    constexpr std::size_t THRESHOLD = Detail::EpochThreadLocalState::K_RETIRE_THRESHOLD;
    constexpr std::size_t RETIRED = 10 * THRESHOLD;

    ThreadPark park;
    std::thread reader([&park]
    {
        EpochGuard guard;
        park.report_and_wait();
    });
    park.wait_for_reports(1);

    Counter destroyed = make_counter();
    const std::size_t scans_before = Detail::tls_state().scan_count;
    for (std::size_t i = 0; i < RETIRED; ++i)
        EpochManager::instance().retire(new Tracked(destroyed));
    const std::size_t scans = Detail::tls_state().scan_count - scans_before;

    // The reader entered before any of these was retired, so none of them is reclaimable.
    EXPECT_EQ(destroyed->load(), 0);
    EXPECT_GE(scans, 1u);
    EXPECT_LE(scans, RETIRED / THRESHOLD);

    park.release();
    reader.join();
}

namespace
{
    /** @brief Holds a payload whose destructor retires a Tracked child. */
    struct RetiringParent
    {
        Counter destroyed;       /**< Bumped when this parent is destroyed. */
        Counter child_destroyed; /**< Handed to the child this parent retires. */

        RetiringParent(Counter parent, Counter child)
            : destroyed(std::move(parent)), child_destroyed(std::move(child))
        {

        }

        ~RetiringParent()
        {
            destroyed->fetch_add(1, std::memory_order_relaxed);
            EpochManager::instance().retire(new Tracked(child_destroyed));
        }
    };
}

/** @brief Tests that a destructor that retires leaves every parent and child deleted once. */
TEST(Medium_EpochReclamationTest, ADestructorThatRetiresDeletesParentAndChildOnce)
{
    constexpr std::size_t THRESHOLD = Detail::EpochThreadLocalState::K_RETIRE_THRESHOLD;
    constexpr int PARENTS = static_cast<int>(4 * THRESHOLD);
    constexpr std::size_t DRAIN_RETIRES = 32 * THRESHOLD;

    Counter parents_destroyed = make_counter();
    Counter children_destroyed = make_counter();
    for (int i = 0; i < PARENTS; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(
            new RetiringParent(parents_destroyed, children_destroyed));
    }

    // Each scan advances the epoch once; 32 of them outlast the parents and their children.
    Counter drain_sink = make_counter();
    for (std::size_t i = 0; i < DRAIN_RETIRES; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(drain_sink));
    }

    EXPECT_EQ(parents_destroyed->load(), PARENTS);
    EXPECT_EQ(children_destroyed->load(), PARENTS);
}

/**
 * @brief Tests that orphaned nodes from an exited thread are correctly reclaimed.
 *
 * Simulates a thread exiting with a retire list, which is then donated to the
 * global orphan list and must be freed by survivors. A prior bug returned
 * orphans to the wrong free pool.
 */
TEST(Medium_EpochReclamation, OrphanedNodesReclaimed)
{
    // The counter is a shared_ptr, as reclamation frees Tracked after this frame; see README.md.
    using Counter = std::shared_ptr<std::atomic<int>>;
    struct Tracked
    {
        Counter destroyed;
        explicit Tracked(Counter d) : destroyed(std::move(d)) {}
        ~Tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
    };

    Counter destroyed = std::make_shared<std::atomic<int>>(0);

    std::thread producer([destroyed]()
    {
        for (int i = 0; i < 300; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
    });
    producer.join();

    Counter sink = std::make_shared<std::atomic<int>>(0);
    for (int i = 0; i < 1024; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(sink));
    }

    EXPECT_GT(destroyed->load(), 0)
        << "Orphaned nodes from an exited thread were not reclaimed (leak regression).";
}

/** @brief Tests epoch reclamation under sustained churn and thread turnover, at scale. */
namespace EpochStressTests
{
    namespace
    {
        /**
         * @brief Counts object lifecycles for one stress run.
         *
         * Counters are heap-owned by shared_ptr: reclamation is deferred, so a tail of retired
         * objects outlives the test and is freed later. Raw pointers to stack atomics would then
         * be written after the frame is gone.
         */
        struct Counters
        {
            std::atomic<long> live{0};       // net live (constructed - destroyed)
            std::atomic<long> destroyed{0};
        };
        using CountersPtr = std::shared_ptr<Counters>;

        /**
         * @brief Creates the counters of one stress run.
         * @return A shared pointer to zeroed counters.
         */
        CountersPtr make_counters()
        {
            return std::make_shared<Counters>();
        }

        /** @brief Counts itself in the live count while it exists and in the destroyed count after. */
        struct CountedObject
        {
            CountersPtr counters;
            explicit CountedObject(CountersPtr shared) : counters(std::move(shared))
            {
                counters->live.fetch_add(1, std::memory_order_relaxed);
            }
            ~CountedObject()
            {
                counters->live.fetch_sub(1, std::memory_order_relaxed);
                counters->destroyed.fetch_add(1, std::memory_order_relaxed);
            }
        };

        /**
         * @brief Retires @p count counted objects on the calling thread, one guard each.
         * @param counters Counters the objects report to.
         * @param count Number of objects to retire.
         */
        void retire_objects(const CountersPtr& counters, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new CountedObject(counters));
            }
        }

        /**
         * @brief Retires objects from several threads at once and joins them.
         * @param counters Counters the objects report to.
         * @param threads Number of threads.
         * @param per_thread Number of objects each thread retires.
         */
        void retire_from_threads(const CountersPtr& counters, int threads, int per_thread)
        {
            std::vector<std::thread> workers;
            for (int t = 0; t < threads; ++t)
                workers.emplace_back([counters, per_thread]() { retire_objects(counters, per_thread); });
            for (auto& worker : workers)
                worker.join();
        }

        /**
         * @brief Runs one short-lived thread per round, each retiring objects and exiting.
         * @param counters Counters the objects report to.
         * @param rounds Number of threads to run, one after the other.
         * @param per_round Number of objects each thread retires.
         */
        void retire_from_short_lived_threads(const CountersPtr& counters, int rounds, int per_round)
        {
            for (int r = 0; r < rounds; ++r)
            {
                std::thread shortlived([counters, per_round]() { retire_objects(counters, per_round); });
                shortlived.join();
            }
        }

        /** @brief Keeps retiring on a thread of its own, which advances the epochs, until it is stopped. */
        class BackgroundDrainer final
        {
            public:
                /** @brief Starts the thread, which retires objects that report to @p counters. */
                explicit BackgroundDrainer(CountersPtr counters)
                    : thread_([this, counters]()
                      {
                          while (running_.load(std::memory_order_relaxed))
                              retire_objects(counters, 1);
                      })
                {
                }

                /** @brief Stops and joins the thread. */
                ~BackgroundDrainer()
                {
                    running_.store(false, std::memory_order_relaxed);
                    thread_.join();
                }

                BackgroundDrainer(const BackgroundDrainer&) = delete;
                BackgroundDrainer& operator=(const BackgroundDrainer&) = delete;

            private:
                /// @brief Cleared to stop the thread.
                std::atomic<bool> running_{true};

                /// @brief The thread; declared last, so every other member exists when it starts.
                std::thread thread_;
        };
    } // namespace

    /**
     * @brief Tests that sustained multi-thread churn reclaims most retired objects.
     *
     * After all threads join and the main thread drains, the destroyed count must dominate and
     * the live count must not go negative.
     */
    TEST(Large_EpochStress, HighChurnNoLeak)
    {
        constexpr int K_THREADS    = 8;
        constexpr int K_PER_THREAD = 20'000;
        constexpr int K_DRAIN      = 4096;

        CountersPtr counters = make_counters();
        retire_from_threads(counters, K_THREADS, K_PER_THREAD);
        retire_objects(make_counters(), K_DRAIN);

        const long total = static_cast<long>(K_THREADS) * K_PER_THREAD;
        EXPECT_GT(counters->destroyed.load(), total / 2)
            << "Most retired objects should have been reclaimed.";
        EXPECT_GE(counters->live.load(), 0) << "Negative live count indicates a double-free.";
    }

    /**
     * @brief Tests orphan handoff by repeatedly spawning short-lived threads.
     *
     * Short-lived threads retire and exit, forcing the orphan handoff on every join while a
     * drainer keeps advancing epochs.
     */
    TEST(Large_EpochStress, ThreadSpawnExitHandoff)
    {
        constexpr int K_ROUNDS    = 64;
        constexpr int K_PER_ROUND = 1000;

        CountersPtr counters = make_counters();
        {
            BackgroundDrainer drainer(make_counters());
            retire_from_short_lived_threads(counters, K_ROUNDS, K_PER_ROUND);
        }

        EXPECT_GT(counters->destroyed.load(), 0)
            << "Short-lived threads' orphaned retirees were never reclaimed.";
        EXPECT_GE(counters->live.load(), 0) << "Negative live count indicates a double-free.";
    }
} // namespace EpochStressTests
