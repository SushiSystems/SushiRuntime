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
 * @file test_affinity.cpp
 * @brief Tests that pinning binds a valid core, leaves the affinity alone for an invalid one, and repeats.
 * @see tests/platform/topology/README.md
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

using namespace SushiRuntime::Internal::Topology;

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

    /** @brief Requests @p times pins from a fresh worker thread and returns its affinity around the requests. */
    AffinityAroundPin pin_from_worker(int core_id, int times = 1)
    {
        AffinityAroundPin observed;
        std::thread worker([&observed, core_id, times]()
        {
            observed.before = calling_thread_affinity();
            for (int request = 0; request < times; ++request)
                pin_current_thread_to_core(core_id);
            observed.after = calling_thread_affinity();
        });
        worker.join();
        return observed;
    }
} // namespace

class Medium_AffinityRefused : public ::testing::TestWithParam<int>
{
};

/** @brief Tests that a core the machine does not have leaves the affinity of the thread as it was. */
TEST_P(Medium_AffinityRefused, ACoreOutOfRangeLeavesAffinityUnchanged)
{
    const AffinityAroundPin observed = pin_from_worker(GetParam());

    ASSERT_FALSE(observed.before.empty()) << "the OS did not report the thread's affinity";
    EXPECT_EQ(observed.after, observed.before);
}

INSTANTIATE_TEST_SUITE_P(Cores, Medium_AffinityRefused, ::testing::Values(-1, 100000),
                         [](const ::testing::TestParamInfo<int>& info)
                         { return info.param < 0 ? std::string("Negative") : std::string("BeyondTheMachine"); });

TEST(Medium_AffinityTest, CoreZeroBecomesTheOnlyCoreOfTheThread)
{
    const AffinityAroundPin observed = pin_from_worker(0);

    ASSERT_FALSE(observed.before.empty()) << "the OS did not report the thread's affinity";
    if (!lists_core_zero(observed.before))
        GTEST_SKIP() << "core 0 is outside the affinity this process may use: " << observed.before;
    EXPECT_EQ(observed.after, K_CORE_ZERO_ALONE);
}

/** @brief Tests that pinning to core 0 again and again keeps core 0 as the only core of the thread. */
TEST(Medium_AffinityTest, PinIsRepeatable)
{
    static constexpr int K_PINS = 16;

    const AffinityAroundPin observed = pin_from_worker(0, K_PINS);

    ASSERT_FALSE(observed.before.empty()) << "the OS did not report the thread's affinity";
    if (!lists_core_zero(observed.before))
        GTEST_SKIP() << "core 0 is outside the affinity this process may use: " << observed.before;
    EXPECT_EQ(observed.after, K_CORE_ZERO_ALONE);
}
