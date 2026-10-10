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
 * @brief Tests that pinning to a valid core, an invalid core or one core repeatedly does not throw.
 * @see modules/platform/topology/tests/README.md
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
