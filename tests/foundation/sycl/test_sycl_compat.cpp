/**************************************************************************/
/* test_sycl_compat.cpp                                                   */
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
 * @file test_sycl_compat.cpp
 * @brief Tests the host task trait and enqueue_host_task() on a host queue.
 * @see tests/foundation/sycl/README.md
 * @author Mustafa Garip
 */

#include <atomic>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sycl/sycl_compat.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names a handler with no host_task member. */
    struct NoHostTask
    {

    };
}

/** @brief Tests that the trait sees host_task on a SYCL handler and not on a plain type. */
TEST(Unit_SYCLCompat, TheTraitSeesHostTask)
{
    const auto function = [] {};
    static_assert(!Detail::has_host_task_v<NoHostTask, decltype(function)>, "no member");
    EXPECT_FALSE((Detail::has_host_task_v<NoHostTask, decltype(function)>));
}

/** @brief Tests that a host task enqueued through the shim runs once before wait() returns. */
TEST(Unit_SYCLCompat, AHostTaskRunsOnce)
{
    sycl::queue queue;
    std::atomic<int> calls{0};
    queue.submit([&](sycl::handler& handler)
    {
        Detail::enqueue_host_task(handler, [&calls] { ++calls; });
    }).wait();
    EXPECT_EQ(calls.load(), 1);
}
