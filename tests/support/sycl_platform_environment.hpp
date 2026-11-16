/**************************************************************************/
/* sycl_platform_environment.hpp                                          */
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
 * @file sycl_platform_environment.hpp
 * @brief Declares SYCLPlatformEnvironment, which loads the SYCL adapters once before a test program's first case.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdio>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

namespace TestSupport
{
/**
 * @brief Loads the SYCL adapters once, so the first case that reaches a device is not charged for them.
 *
 * A death test's child skips the load: it runs one statement, and the adapters cost each child about 1 GB.
 */
class SYCLPlatformEnvironment final : public ::testing::Environment
{
    public:
        /** @brief Discovers the platforms unless the process is a death test's child. */
        void SetUp() override
        {
            if (!GTEST_FLAG_GET(internal_run_death_test).empty())
                return;
            try
            {
                static_cast<void>(sycl::platform::get_platforms());
            }
            catch (const sycl::exception& error)
            {
                std::fprintf(stderr, "SYCL platform discovery failed before the tests: %s\n", error.what());
            }
        }
};

/**
 * @brief Registers the environment once per program, whatever number of units include this header.
 */
inline ::testing::Environment* const K_SYCL_PLATFORM_ENVIRONMENT =
    ::testing::AddGlobalTestEnvironment(new SYCLPlatformEnvironment());
} // namespace TestSupport
