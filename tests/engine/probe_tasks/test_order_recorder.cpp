/**************************************************************************/
/* test_order_recorder.cpp                                                */
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
 * @file test_order_recorder.cpp
 * @brief Tests make_order_recorder(): the callable appends its id under the mutex.
 * @see tests/engine/probe_tasks/README.md
 * @author Mustafa Garip
 */

#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/probe_tasks/order_recorder.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Fixture of the tests that hand the program's queue to a host callable. */
class Medium_OrderRecorder : public TestSupport::RuntimeTest
{
};

/** @brief Tests that each recorder appends its own id, in the order they are called. */
TEST_F(Medium_OrderRecorder, AppendsItsIdWhenCalled)
{
    std::vector<int> order;
    std::mutex order_mutex;
    auto first = make_order_recorder(1, order, order_mutex);
    auto second = make_order_recorder(2, order, order_mutex);

    (void)second(queue(), {});
    (void)first(queue(), {});
    (void)second(queue(), {});

    EXPECT_EQ(order, (std::vector<int>{2, 1, 2}));
}
