/**************************************************************************/
/* test_affinity.cpp                                                      */
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
 * @file test_affinity.cpp
 * @brief Tests that pinning binds a valid core, refuses an invalid one and never throws.
 * @see modules/platform/topology/tests/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/topology/affinity.hpp>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sched.h>
#endif

using namespace SushiRuntime::Topology;

namespace
{
    /** @brief Spells the affinity of core 0 alone, in the form calling_thread_affinity() returns. */
    const char* const K_CORE_ZERO_ALONE = "0";

#ifdef _WIN32
    /** @brief Reads the calling thread's affinity from the OS as "group:core,core"; empty on failure. */
    std::string calling_thread_affinity()
    {
        GROUP_AFFINITY affinity = {};
        if (GetThreadGroupAffinity(GetCurrentThread(), &affinity) == 0)
            return std::string();

        std::string text;
        for (int core = 0; core < 64; ++core)
        {
            if ((affinity.Mask & (static_cast<KAFFINITY>(1) << core)) == 0)
                continue;
            if (!text.empty())
                text += ',';
            text += std::to_string(static_cast<int>(affinity.Group) * 64 + core);
        }
        return text;
    }
#else
    /** @brief Reads the calling thread's affinity from the OS as "core,core"; empty on failure. */
    std::string calling_thread_affinity()
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
            return std::string();

        std::string text;
        for (int core = 0; core < CPU_SETSIZE; ++core)
        {
            if (!CPU_ISSET(core, &cpuset))
                continue;
            if (!text.empty())
                text += ',';
            text += std::to_string(core);
        }
        return text;
    }
#endif

    /** @brief Returns true when a calling_thread_affinity() text lists core 0. */
    bool lists_core_zero(const std::string& affinity)
    {
        return affinity == K_CORE_ZERO_ALONE || affinity.rfind("0,", 0) == 0;
    }

    /** @brief Holds a worker thread's affinity as read before and after one pin request. */
    struct AffinityAroundPin
    {
        std::string before{}; /**< Affinity before the request. */
        std::string after{};  /**< Affinity after the request. */
    };

    /** @brief Requests a pin from a fresh worker thread and returns its affinity around the request. */
    AffinityAroundPin pin_from_worker(int core_id)
    {
        AffinityAroundPin observed;
        std::thread worker([&observed, core_id]()
        {
            observed.before = calling_thread_affinity();
            pin_current_thread_to_core(core_id);
            observed.after = calling_thread_affinity();
        });
        worker.join();
        return observed;
    }
} // namespace

TEST(Unit_AffinityTest, NegativeCoreLeavesAffinityUnchanged)
{
    const AffinityAroundPin observed = pin_from_worker(-1);

    ASSERT_FALSE(observed.before.empty()) << "the OS did not report the thread's affinity";
    EXPECT_EQ(observed.after, observed.before);
}

TEST(Unit_AffinityTest, CoreBeyondTheMachineLeavesAffinityUnchanged)
{
    const AffinityAroundPin observed = pin_from_worker(100000);

    ASSERT_FALSE(observed.before.empty()) << "the OS did not report the thread's affinity";
    EXPECT_EQ(observed.after, observed.before);
}

TEST(Unit_AffinityTest, CoreZeroBecomesTheOnlyCoreOfTheThread)
{
    const AffinityAroundPin observed = pin_from_worker(0);

    ASSERT_FALSE(observed.before.empty()) << "the OS did not report the thread's affinity";
    if (!lists_core_zero(observed.before))
        GTEST_SKIP() << "core 0 is outside the affinity this process may use: " << observed.before;
    EXPECT_EQ(observed.after, K_CORE_ZERO_ALONE);
}

/**
 * @brief Tests that pinning a worker thread to a valid core does not throw.
 *
 * Pinning to core 0 (always present) should be successful or at least
 * handled gracefully internally.
 */
TEST(Unit_AffinityTest, PinToValidCore)
{
    bool threw = false;
    std::thread worker([&threw]()
    {
        try
        {
            pin_current_thread_to_core(0);
        }
        catch (...)
        {
            threw = true;
        }
    });
    worker.join();

    EXPECT_FALSE(threw) << "Pinning to a valid core should never throw.";
}

/**
 * @brief Tests that an out-of-range core id fails gracefully.
 *
 * Requesting a non-existent core must trigger a warning but not throw an
 * exception or crash the program.
 */
TEST(Unit_AffinityTest, PinToInvalidCoreIsGraceful)
{
    bool threw = false;
    std::thread worker([&threw]()
    {
        try
        {
            pin_current_thread_to_core(100000);
        }
        catch (...)
        {
            threw = true;
        }
    });
    worker.join();

    EXPECT_FALSE(threw) << "Pinning to an invalid core must fail gracefully, not throw.";
}

/**
 * @brief Tests that repeated pinning on the same thread remains stable.
 *
 * Calling the pinning function multiple times should not cause any exceptions
 * or cumulative failure states.
 */
TEST(Unit_AffinityTest, PinIsRepeatable)
{
    bool threw = false;
    std::thread worker([&threw]()
    {
        try
        {
            for (int i = 0; i < 16; ++i)
                pin_current_thread_to_core(0);
        }
        catch (...)
        {
            threw = true;
        }
    });
    worker.join();

    EXPECT_FALSE(threw);
}
