/****************************************************************/
/* test_affinity.cpp                                            */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_affinity.cpp
 * @brief Tests that pinning to a valid core, an invalid core or one core repeatedly does not throw.
 * @see tests/functional/unit/README.md
 * @author Mustafa Garip
 */

#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/topology/affinity.hpp>

using namespace SushiRuntime::Topology;

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
