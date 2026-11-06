/**************************************************************************/
/* test_device_descriptor.cpp                                             */
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
 * @file test_device_descriptor.cpp
 * @brief Tests DeviceDescriptor: it carries a SYCL device and the description of it.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <string>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/machine/device_kind.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Device::DeviceDescriptor;

/** @brief Tests that a descriptor keeps the device and the description it was given. */
TEST(Medium_DeviceDescriptor, KeepsTheDeviceAndItsDescription)
{
    if (sycl::device::get_devices().empty())
        GTEST_SKIP() << "no SYCL device";
    const sycl::device device = sycl::device::get_devices().front();

    DeviceDescriptor descriptor;
    descriptor.device = device;
    descriptor.info.index = 2;
    descriptor.info.name = device.get_info<sycl::info::device::name>();
    descriptor.info.kind = DeviceKind::GPU;

    EXPECT_EQ(descriptor.device, device);
    EXPECT_EQ(descriptor.info.index, 2u);
    EXPECT_EQ(descriptor.info.name, device.get_info<sycl::info::device::name>());
    EXPECT_EQ(descriptor.info.kind, DeviceKind::GPU);
}
