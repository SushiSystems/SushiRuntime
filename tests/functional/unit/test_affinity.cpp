/**************************************************************************/
/* test_affinity.cpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Thread-to-core pinning is best-effort: it logs a warning on failure rather
// than throwing. These tests assert that contract, run from a worker thread so
// the test runner's own affinity is left untouched.

#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/topology/affinity.hpp>

using namespace SushiRuntime::Topology;

// Pinning a worker thread to core 0 (always present) must not throw.
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

// An out-of-range core id must fail gracefully (warning), not throw or crash.
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

// Repeated pinning on the same thread must stay stable.
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
