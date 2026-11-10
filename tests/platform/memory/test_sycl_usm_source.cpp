/**************************************************************************/
/* test_sycl_usm_source.cpp                                               */
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
 * @file test_sycl_usm_source.cpp
 * @brief Tests the SYCL USMSource against the USMSource contract on the program's device.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/memory/sycl_queues.hpp>
#include <SushiRuntime/memory/sycl_usm_source.hpp>

#include "support/runtime_environment.hpp"
#include "support/usm_source_contract.hpp"

using SushiRuntime::Internal::Memory::make_sycl_queues;
using SushiRuntime::Internal::Memory::make_sycl_usm_source;
using SushiRuntime::Internal::Memory::USMSource;

namespace
{
    /** @brief Makes the source the contract runs on: the program's one queue. */
    struct SYCLSourceMaker
    {
        /** @brief Returns a SYCL source over the program's queue. */
        static std::unique_ptr<USMSource> make()
        {
            return make_sycl_usm_source(make_sycl_queues(
                std::vector<sycl::queue>{TestSupport::RuntimeEnvironment::queue()}));
        }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the USMSource contract on the device, the boundary the host source stands in for. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_SYCLUSMSource, USMSourceContract, ::SYCLSourceMaker, );
} // namespace TestSupport
