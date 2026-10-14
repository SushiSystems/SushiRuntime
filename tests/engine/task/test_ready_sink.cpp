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
TEST(Unit_ReadySink, IsAnOrderedListOfTasks)
{
    static_assert(std::is_same_v<ReadySink, std::vector<Task*>>, "the sink is a vector of tasks");
    ReadySink sink;
    Task* const first = reinterpret_cast<Task*>(0x10);
    Task* const second = reinterpret_cast<Task*>(0x20);
    sink.push_back(first);
    sink.push_back(second);
    EXPECT_EQ(sink, (ReadySink{first, second}));
}
