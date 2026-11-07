/**************************************************************************/
/* event_readiness_contract.hpp                                          */
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
 * @file event_readiness_contract.hpp
 * @brief Declares EventReadinessContract, the cases every EventReadiness adapter passes.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/scheduler/event_readiness.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the readiness contract.
     * @tparam Maker A type with a static make() returning std::unique_ptr<EventReadiness> and a
     *         static event() returning an event the adapter can wait on.
     */
    template <class Maker>
    class EventReadinessContract : public ::testing::Test
    {
        protected:
            /** @brief Makes the adapter each case runs on. */
            void SetUp() override { readiness_ = Maker::make(); }

            /** @brief Returns the adapter under test. */
            [[nodiscard]] SushiRuntime::Internal::Scheduler::EventReadiness& readiness() { return *readiness_; }

        private:
            /** @brief The adapter under test, one per case. */
            std::unique_ptr<SushiRuntime::Internal::Scheduler::EventReadiness> readiness_;
    };

    TYPED_TEST_SUITE_P(EventReadinessContract);

    /** @brief Tests that an event the adapter waited on is complete. */
    TYPED_TEST_P(EventReadinessContract, AWaitedEventIsComplete)
    {
        sycl::event event = TypeParam::event();

        this->readiness().wait(event);

        EXPECT_TRUE(this->readiness().is_complete(event));
    }

    REGISTER_TYPED_TEST_SUITE_P(EventReadinessContract, AWaitedEventIsComplete);
} // namespace TestSupport
