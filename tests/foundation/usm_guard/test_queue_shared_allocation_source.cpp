/**************************************************************************/
/* test_queue_shared_allocation_source.cpp                                */
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
 * @file test_queue_shared_allocation_source.cpp
 * @brief Tests the queue SharedAllocationSource against the SharedAllocationSource contract on the device.
 * @see tests/foundation/usm_guard/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/usm_guard/queue_shared_allocation_source.hpp>

#include "support/runtime_environment.hpp"
#include "support/shared_allocation_source_contract.hpp"

namespace
{
    /** @brief Makes the source the contract runs on: the program's queue. */
    struct QueueSourceMaker
    {
        /** @brief Returns the source under test. */
        static std::unique_ptr<SushiRuntime::Test::SharedAllocationSource> make()
        {
            return std::make_unique<SushiRuntime::Test::QueueSharedAllocationSource>(TestSupport::RuntimeEnvironment::queue());
        }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the SharedAllocationSource contract on the device, the boundary host memory stands in for. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_QueueSharedAllocationSource, SharedAllocationSourceContract, ::QueueSourceMaker, );
} // namespace TestSupport
