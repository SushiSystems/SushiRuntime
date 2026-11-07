/**************************************************************************/
/* test_device_filter.cpp                                                 */
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

/**
 * @file test_device_filter.cpp
 * @brief Tests admits_class() and admits(): which devices a DeviceFilter accepts, decided on values.
 * @see tests/foundation/machine/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/machine/device_filter.hpp>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_kind.hpp>

using SushiRuntime::admits;
using SushiRuntime::admits_class;
using SushiRuntime::DeviceFilter;
using SushiRuntime::DeviceKind;

namespace
{
    /** @brief Returns a filter that accepts no device class. */
    DeviceFilter filter_admitting_no_class()
    {
        DeviceFilter filter;
        filter.include_cpu = false;
        filter.include_gpu = false;
        filter.include_accelerator = false;
        return filter;
    }
} // namespace

class Small_DeviceFilterClass : public ::testing::TestWithParam<DeviceKind>
{
};

/** @brief Tests that the default filter accepts a device of every class, with or without fp64. */
TEST_P(Small_DeviceFilterClass, TheDefaultFilterAcceptsTheClass)
{
    EXPECT_TRUE(admits(DeviceFilter{}, GetParam(), false));
    EXPECT_TRUE(admits(DeviceFilter{}, GetParam(), true));
}

/** @brief Tests that a filter that turns every listed class off accepts only a device of no listed class. */
TEST_P(Small_DeviceFilterClass, AFilterThatAdmitsNoClassAcceptsOnlyAnUnlistedClass)
{
    EXPECT_EQ(admits_class(filter_admitting_no_class(), GetParam()), GetParam() == DeviceKind::Other);
}

INSTANTIATE_TEST_SUITE_P(Classes, Small_DeviceFilterClass,
                         ::testing::Values(DeviceKind::CPU, DeviceKind::GPU, DeviceKind::Accelerator,
                                           DeviceKind::Other));

/** @brief Tests that turning one class off drops that class and keeps the others. */
TEST(Small_DeviceFilter, TurningOneClassOffDropsThatClassAlone)
{
    DeviceFilter filter;
    filter.include_gpu = false;

    EXPECT_FALSE(admits_class(filter, DeviceKind::GPU));
    EXPECT_TRUE(admits_class(filter, DeviceKind::CPU));
    EXPECT_TRUE(admits_class(filter, DeviceKind::Accelerator));
}

/** @brief Tests that require_fp64 drops a device without fp64 and keeps one with it. */
TEST(Small_DeviceFilter, RequireFp64DropsADeviceWithoutIt)
{
    DeviceFilter filter;
    filter.require_fp64 = true;

    EXPECT_FALSE(admits(filter, DeviceKind::GPU, false));
    EXPECT_TRUE(admits(filter, DeviceKind::GPU, true));
}
