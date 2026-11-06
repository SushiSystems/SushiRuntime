/**************************************************************************/
/* test_atomic_counter_task.cpp                                           */
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
 * @file test_atomic_counter_task.cpp
 * @brief Tests AtomicCounterTask: a host task that counts each run.
 * @see tests/engine/probe_tasks/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/probe_tasks/atomic_counter_task.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Fixture of the tests that hand the program's queue to a host task. */
class Medium_AtomicCounterTask : public TestSupport::RuntimeTest
{
};

/** @brief Tests that each run adds one to the counter. */
TEST_F(Medium_AtomicCounterTask, EachRunAddsOne)
{
    std::atomic<std::size_t> counter{0};
    AtomicCounterTask task(counter);

    (void)task.execute_host(queue(), {});
    (void)task.execute_host(queue(), {});

    EXPECT_EQ(counter.load(), 2u);
}
