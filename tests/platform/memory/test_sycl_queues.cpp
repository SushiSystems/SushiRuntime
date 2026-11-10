/**************************************************************************/
/* test_sycl_queues.cpp                                                   */
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
 * @file test_sycl_queues.cpp
 * @brief Tests make_sycl_queues(): the queues it wraps are the ones the SYCL source counts.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/memory/sycl_queues.hpp>
#include <SushiRuntime/memory/sycl_usm_source.hpp>

using SushiRuntime::Internal::Memory::make_sycl_queues;
using SushiRuntime::Internal::Memory::make_sycl_usm_source;

namespace
{
    TEST(Small_SYCLQueues, NoQueuesMakeASourceOfNoDevices)
    {
        const auto source = make_sycl_usm_source(make_sycl_queues(std::vector<sycl::queue>{}));

        EXPECT_EQ(source->device_count(), 0u);
    }
} // namespace
