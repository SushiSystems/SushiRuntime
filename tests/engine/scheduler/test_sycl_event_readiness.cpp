/**************************************************************************/
/* test_sycl_event_readiness.cpp                                         */
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
 * @file test_sycl_event_readiness.cpp
 * @brief Tests SYCLEventReadiness against the readiness contract on the program's queue.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/scheduler/sycl_event_readiness.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>

#include "support/event_readiness_contract.hpp"
#include "support/runtime_environment.hpp"

namespace
{
    /** @brief Makes the SYCL adapter and an event of an empty host task on the program's queue. */
    struct SYCLReadinessMaker
    {
        /** @brief Returns the adapter under test. */
        static std::unique_ptr<SushiRuntime::Internal::Scheduler::EventReadiness> make()
        {
            return std::make_unique<SushiRuntime::Internal::Scheduler::SYCLEventReadiness>();
        }

        /** @brief Returns the event of an empty host task on the program's queue. */
        static sycl::event event()
        {
            return TestSupport::RuntimeEnvironment::queue().submit([](sycl::handler& handler)
            {
                SushiRuntime::Detail::enqueue_host_task(handler, []() {});
            });
        }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the EventReadiness contract on the SYCL runtime. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_SYCLEventReadiness, EventReadinessContract, ::SYCLReadinessMaker, );
} // namespace TestSupport
