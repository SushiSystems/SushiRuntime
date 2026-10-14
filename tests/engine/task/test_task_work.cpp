/**************************************************************************/
/* test_task_work.cpp                                                     */
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
 * @file test_task_work.cpp
 * @brief Tests TaskWork and HostWork: the two callables a node runs.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/task/task_work.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests that host work runs on a queue and returns its event. */
TEST(Unit_TaskWork, HostWorkRunsOnAQueue)
{
    int calls = 0;
    const HostWork work = [&calls](sycl::queue&, const std::vector<sycl::event>& dependencies)
    {
        calls += static_cast<int>(dependencies.size()) + 1;
        return sycl::event();
    };
    sycl::queue queue;
    work(queue, {sycl::event()}).wait();
    EXPECT_EQ(calls, 2);
}

/** @brief Tests that a command group runs inside a submit. */
TEST(Unit_TaskWork, TaskWorkRunsInsideASubmit)
{
    bool called = false;
    const TaskWork work = [&called](sycl::handler&) { called = true; };
    sycl::queue queue;
    queue.submit([&work](sycl::handler& handler) { work(handler); }).wait();
    EXPECT_TRUE(called);
}
