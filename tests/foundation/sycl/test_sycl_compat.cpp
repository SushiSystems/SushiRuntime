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
 * @file test_sycl_compat.cpp
 * @brief Tests the host task trait and enqueue_host_task() on the program's queue.
 * @see tests/foundation/sycl/README.md
 * @author Mustafa Garip
 */

#include <atomic>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sycl/sycl_compat.hpp>

#include <support/runtime_environment.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names a handler with no host_task member. */
    struct NoHostTask
    {

    };

    /** @brief Names a handler with a host_task member. */
    struct WithHostTask
    {
        /** @brief Accepts the callable and does nothing with it. */
        template <class Fn>
        void host_task(Fn&&)
        {

        }
    };

    /** @brief Gives the host task tests the device queue of the program. */
    class Medium_SYCLCompat : public TestSupport::RuntimeTest
    {
    };
}

/** @brief Tests that the trait sees a host_task member and does not see its absence. */
TEST(Small_SYCLCompat, TheTraitSeesAHostTaskMemberAndNoOther)
{
    const auto function = [] {};

    EXPECT_TRUE((Detail::has_host_task_v<WithHostTask, decltype(function)>));
    EXPECT_FALSE((Detail::has_host_task_v<NoHostTask, decltype(function)>));
}

/** @brief Tests that a host task enqueued through the shim runs once before wait() returns. */
TEST_F(Medium_SYCLCompat, AHostTaskRunsOnce)
{
    std::atomic<int> calls{0};
    queue().submit([&](sycl::handler& handler)
    {
        Detail::enqueue_host_task(handler, [&calls] { ++calls; });
    }).wait();
    EXPECT_EQ(calls.load(), 1);
}
