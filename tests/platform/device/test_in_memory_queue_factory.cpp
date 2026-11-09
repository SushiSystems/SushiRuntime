/**************************************************************************/
/* test_in_memory_queue_factory.cpp                                       */
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
 * @file test_in_memory_queue_factory.cpp
 * @brief Tests the in-memory QueueFactory against the QueueFactory contract and its own promises.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/in_memory_queue_factory.hpp>
#include <SushiRuntime/device/queue_factory.hpp>

#include "support/queue_factory_contract.hpp"

using SushiRuntime::Internal::Device::InMemoryQueueFactory;
using SushiRuntime::Internal::Device::QueueBuild;
using SushiRuntime::Internal::Device::QueueFactory;

namespace
{
    /** @brief Makes the factory the contract runs on: an in-memory factory of two devices. */
    struct InMemoryFactoryMaker
    {
        /** @brief Returns an in-memory factory of two devices. */
        static std::unique_ptr<QueueFactory> make() { return std::make_unique<InMemoryQueueFactory>(2); }
    };
} // namespace

namespace TestSupport
{
    /**
     * @brief Runs the QueueFactory contract on the in-memory factory.
     * @note Medium: make() returns a sycl::queue, which the factory builds on the CPU device. The runtime
     *       keeps sycl::queue as its execution currency (decision 64), so the rows that build one need a device.
     */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_InMemoryQueueFactory, QueueFactoryContract, ::InMemoryFactoryMaker, );
} // namespace TestSupport

/** @brief Tests that the factory records every build in order, with its device, flavour and context. */
TEST(Small_InMemoryQueueFactory, RecordsEveryBuildInOrder)
{
    InMemoryQueueFactory factory(2);
    const sycl::queue plain = factory.make(1, {}, nullptr);
    const sycl::context context = plain.get_context();

    (void)factory.make(1, {true, true}, &context);

    const std::vector<QueueBuild> builds = factory.builds();
    ASSERT_EQ(builds.size(), 2u);
    EXPECT_EQ(builds[0].device, 1u);
    EXPECT_FALSE(builds[0].shared);
    EXPECT_TRUE(builds[1].flavour.in_order);
    EXPECT_TRUE(builds[1].flavour.profiling);
    EXPECT_TRUE(builds[1].shared);
}

/** @brief Tests that a device needs an in-order plain queue only when the list says so. */
TEST(Small_InMemoryQueueFactory, NeedsInOrderFollowsTheList)
{
    const InMemoryQueueFactory factory(3, {false, true});

    EXPECT_FALSE(factory.needs_in_order(0));
    EXPECT_TRUE(factory.needs_in_order(1));
    EXPECT_FALSE(factory.needs_in_order(2));
}
