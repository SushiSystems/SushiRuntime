/**************************************************************************/
/* queue_factory_contract.hpp                                             */
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
 * @file queue_factory_contract.hpp
 * @brief Declares QueueFactoryContract, the typed suite every QueueFactory adapter passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/queue_factory.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the contract; Maker::make() returns the factory under test.
     * @tparam Maker A type with a static make() returning std::unique_ptr<QueueFactory> of one
     *               device or more.
     */
    template <class Maker>
    class QueueFactoryContract : public ::testing::Test
    {
        protected:
            /** @brief Makes the factory each case runs on. */
            void SetUp() override { factory_ = Maker::make(); }

            /** @brief Returns the factory under test. */
            [[nodiscard]] SushiRuntime::Internal::Device::QueueFactory& factory() { return *factory_; }

        private:
            /** @brief The factory under test, one per case. */
            std::unique_ptr<SushiRuntime::Internal::Device::QueueFactory> factory_;
    };

    TYPED_TEST_SUITE_P(QueueFactoryContract);

    /** @brief Tests that a factory holds a device. */
    TYPED_TEST_P(QueueFactoryContract, HoldsADevice)
    {
        EXPECT_GE(this->factory().count(), 1u);
    }

    /** @brief Tests that two builds give two queues. */
    TYPED_TEST_P(QueueFactoryContract, EveryBuildIsAQueueOfItsOwn)
    {
        const sycl::queue first = this->factory().make(0, {}, nullptr);
        const sycl::queue second = this->factory().make(0, {}, nullptr);

        EXPECT_FALSE(first == second);
    }

    /** @brief Tests that an in-order flavour builds an in-order queue. */
    TYPED_TEST_P(QueueFactoryContract, AnInOrderFlavourBuildsAnInOrderQueue)
    {
        const sycl::queue built = this->factory().make(0, {true, false}, nullptr);

        EXPECT_TRUE(built.is_in_order());
    }

    /** @brief Tests that a profiling flavour builds a queue with enable_profiling. */
    TYPED_TEST_P(QueueFactoryContract, AProfilingFlavourBuildsAProfilingQueue)
    {
        const sycl::queue built = this->factory().make(0, {false, true}, nullptr);

        EXPECT_TRUE(built.has_property<sycl::property::queue::enable_profiling>());
        EXPECT_FALSE(built.is_in_order());
    }

    /** @brief Tests that a queue built in a given context belongs to that context. */
    TYPED_TEST_P(QueueFactoryContract, AQueueBuiltInAContextSharesIt)
    {
        const sycl::queue plain = this->factory().make(0, {}, nullptr);
        const sycl::context context = plain.get_context();

        const sycl::queue beside = this->factory().make(0, {false, true}, &context);

        EXPECT_TRUE(beside.get_context() == context);
    }

    /** @brief Tests that a device answers a driver version. */
    TYPED_TEST_P(QueueFactoryContract, ADeviceNamesADriverVersion)
    {
        EXPECT_FALSE(this->factory().driver_version(0).empty());
    }

    REGISTER_TYPED_TEST_SUITE_P(QueueFactoryContract, HoldsADevice, EveryBuildIsAQueueOfItsOwn,
                                AnInOrderFlavourBuildsAnInOrderQueue,
                                AProfilingFlavourBuildsAProfilingQueue,
                                AQueueBuiltInAContextSharesIt, ADeviceNamesADriverVersion);
} // namespace TestSupport
