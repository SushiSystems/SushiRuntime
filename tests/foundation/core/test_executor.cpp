/**************************************************************************/
/* test_executor.cpp                                                      */
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
 * @file test_executor.cpp
 * @brief Tests the Executor contract on both adapters.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/core/thread_pool_executor.hpp>

#include "support/executor_contract.hpp"

using SushiRuntime::SeededExecutor;
using SushiRuntime::ThreadPoolExecutor;

namespace
{
    /** @brief Makes the seeded executor the contract runs on, and drains it on this thread. */
    struct SeededMaker
    {
        /** @brief Returns an executor of seed 5. */
        static std::unique_ptr<SeededExecutor> make() { return std::make_unique<SeededExecutor>(5); }

        /** @brief Runs every pending job. */
        static void drain(SeededExecutor& executor) { executor.run_all(); }
    };

    /** @brief Makes the pool the contract runs on: two workers, waited on until idle. */
    struct ThreadPoolMaker
    {
        /** @brief Returns a pool of two workers. */
        static std::unique_ptr<ThreadPoolExecutor> make()
        {
            return std::make_unique<ThreadPoolExecutor>(2);
        }

        /** @brief Waits until the workers have run every job. */
        static void drain(ThreadPoolExecutor& executor) { executor.wait_idle(); }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the contract on the executor a test drives. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_SeededExecutor, ExecutorContract, ::SeededMaker, );

    /** @brief Runs the contract on real threads, the boundary SeededExecutor stands in for. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_ThreadPoolExecutor, ExecutorContract, ::ThreadPoolMaker, );
} // namespace TestSupport
