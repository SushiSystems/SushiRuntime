/**************************************************************************/
/* test_sycl_device_enumerator.cpp                                        */
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
 * @file test_sycl_device_enumerator.cpp
 * @brief Tests the SYCL DeviceEnumerator against the DeviceEnumerator contract on the machine.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/sycl_device_enumerator.hpp>

#include "support/device_enumerator_contract.hpp"

namespace
{
    /** @brief Makes the enumerator the contract runs on: every SYCL platform of the machine. */
    struct SYCLEnumeratorMaker
    {
        /** @brief Returns the adapter under test. */
        static std::unique_ptr<SushiRuntime::Internal::Topology::DeviceEnumerator> make()
        {
            return std::make_unique<SushiRuntime::Internal::Topology::SYCLDeviceEnumerator>();
        }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the DeviceEnumerator contract on the machine, the boundary the in-memory enumerator stands in for. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_SYCLDeviceEnumerator, DeviceEnumeratorContract, ::SYCLEnumeratorMaker, );
} // namespace TestSupport
