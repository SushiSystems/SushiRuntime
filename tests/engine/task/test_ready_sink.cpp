/**************************************************************************/
/* test_ready_sink.cpp                                                    */
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
 * @file test_ready_sink.cpp
 * @brief Tests ReadySink: the list a completed node writes its ready successors into.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/task/ready_sink.hpp>

using namespace SushiRuntime::Internal::Scheduler;

/** @brief Tests that the sink is a vector of task pointers that keeps their order. */
TEST(Small_ReadySink, IsAnOrderedListOfTasks)
{
    static_assert(std::is_same_v<ReadySink, std::vector<Task*>>, "the sink is a vector of tasks");
    ReadySink sink;
    Task* const first = reinterpret_cast<Task*>(0x10);
    Task* const second = reinterpret_cast<Task*>(0x20);
    sink.push_back(first);
    sink.push_back(second);
    EXPECT_EQ(sink, (ReadySink{first, second}));
}
