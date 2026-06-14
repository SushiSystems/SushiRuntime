/**************************************************************************/
/* test_affinity.cpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_affinity.cpp
 * @brief Unit coverage for thread-to-core pinning.
 *
 * pin_current_thread_to_core is best-effort: it logs a warning on failure
 * rather than throwing. These tests assert that contract -- a valid core
 * succeeds quietly and an out-of-range core never throws or aborts -- run from
 * a worker thread so the test runner's own affinity is left untouched.
 */

#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/topology/affinity.hpp>

using namespace SushiRuntime::Topology;

/**
 * @test PinToValidCore
 * @brief Pinning a worker thread to core 0 (always present) must not throw.
 */
TEST(Unit_AffinityTest, PinToValidCore)
{
    bool threw = false;
    std::thread worker([&threw]()
    {
        try { pin_current_thread_to_core(0); }
        catch (...) { threw = true; }
    });
    worker.join();

    EXPECT_FALSE(threw) << "Pinning to a valid core should never throw.";
}

/**
 * @test PinToInvalidCoreIsGraceful
 * @brief An out-of-range core id is a best-effort failure: it logs a warning
 * but must not throw or crash the process.
 */
TEST(Unit_AffinityTest, PinToInvalidCoreIsGraceful)
{
    bool threw = false;
    std::thread worker([&threw]()
    {
        try { pin_current_thread_to_core(100000); }
        catch (...) { threw = true; }
    });
    worker.join();

    EXPECT_FALSE(threw) << "Pinning to an invalid core must fail gracefully, not throw.";
}

/**
 * @test PinIsRepeatable
 * @brief Repeated pinning on the same thread must remain stable (no leaked
 * handles, no accumulated failure state).
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
        catch (...) { threw = true; }
    });
    worker.join();

    EXPECT_FALSE(threw);
}
