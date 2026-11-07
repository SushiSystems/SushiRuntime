/**************************************************************************/
/* test_in_memory_event_readiness.cpp                                    */
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
 * @file test_in_memory_event_readiness.cpp
 * @brief Tests InMemoryEventReadiness against the readiness contract and its own promises.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/scheduler/in_memory_event_readiness.hpp>

#include "support/event_readiness_contract.hpp"

using SushiRuntime::Internal::Scheduler::InMemoryEventReadiness;

namespace
{
    /** @brief Makes the adapter the contract runs on and the default event it reads nothing of. */
    struct InMemoryReadinessMaker
    {
        /** @brief Returns the adapter under test. */
        static std::unique_ptr<SushiRuntime::Internal::Scheduler::EventReadiness> make()
        {
            return std::make_unique<InMemoryEventReadiness>();
        }

        /** @brief Returns a default event. */
        static sycl::event event() { return sycl::event{}; }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the EventReadiness contract on the in-memory adapter. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_InMemoryEventReadiness, EventReadinessContract, ::InMemoryReadinessMaker, );
} // namespace TestSupport

/** @brief Tests that an event is pending until the adapter is told it completed, and that telling it is no wait. */
TEST(Small_InMemoryEventReadiness, AnEventIsPendingUntilCompleted)
{
    InMemoryEventReadiness readiness;
    const sycl::event event{};

    EXPECT_FALSE(readiness.is_complete(event));
    readiness.complete();
    EXPECT_TRUE(readiness.is_complete(event));
    EXPECT_EQ(readiness.waits(), 0u);
}

/** @brief Tests that the adapter counts every wait. */
TEST(Small_InMemoryEventReadiness, CountsEveryWait)
{
    InMemoryEventReadiness readiness;
    sycl::event event{};

    readiness.wait(event);
    readiness.wait(event);

    EXPECT_EQ(readiness.waits(), 2u);
}
