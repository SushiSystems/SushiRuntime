/**************************************************************************/
/* test_queue_factory.cpp                                                 */
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
 * @file test_queue_factory.cpp
 * @brief Tests properties_of(), the property list of each queue flavour.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/queue_factory.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>

using SushiRuntime::Internal::Device::properties_of;
using SushiRuntime::Internal::Device::QueueFlavour;

/** @brief Tests that the plain flavour asks for no property. */
TEST(Small_QueueFactory, ThePlainFlavourAsksForNoProperty)
{
    const sycl::property_list properties = properties_of(QueueFlavour{});

    EXPECT_FALSE(properties.has_property<sycl::property::queue::in_order>());
    EXPECT_FALSE(properties.has_property<sycl::property::queue::enable_profiling>());
}

/** @brief Tests that each half of a flavour asks for its own property and only that one. */
TEST(Small_QueueFactory, EachHalfOfAFlavourAsksForItsProperty)
{
    const sycl::property_list in_order = properties_of(QueueFlavour{true, false});
    const sycl::property_list profiling = properties_of(QueueFlavour{false, true});
    const sycl::property_list both = properties_of(QueueFlavour{true, true});

    EXPECT_TRUE(in_order.has_property<sycl::property::queue::in_order>());
    EXPECT_FALSE(in_order.has_property<sycl::property::queue::enable_profiling>());
    EXPECT_TRUE(profiling.has_property<sycl::property::queue::enable_profiling>());
    EXPECT_FALSE(profiling.has_property<sycl::property::queue::in_order>());
    EXPECT_TRUE(both.has_property<sycl::property::queue::in_order>());
    EXPECT_TRUE(both.has_property<sycl::property::queue::enable_profiling>());
}
