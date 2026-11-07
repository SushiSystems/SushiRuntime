/**************************************************************************/
/* test_executor_drive.cpp                                                */
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
 * @file test_executor_drive.cpp
 * @brief Tests the TransportDrive contract on ExecutorDrive, and that it reads the clock it was given.
 * @see tests/extension/in_process_transport/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/in_process_transport/executor_drive.hpp>

#include "support/transport_drive_contract.hpp"

using SushiRuntime::ManualClock;
using SushiRuntime::SeededExecutor;
using SushiRuntime::Distributed::ExecutorDrive;
using SushiRuntime::Distributed::TransportDrive;

namespace
{
    /** @brief Makes a drive over a seeded executor and a manual clock, and steps the executor. */
    struct ExecutorDriveMaker
    {
        /** @brief Returns a drive over the maker's executor and clock. */
        std::unique_ptr<TransportDrive> make()
        {
            return std::make_unique<ExecutorDrive>(executor, clock);
        }

        /** @brief Runs every posted job. */
        void settle(TestSupport::RoundCounter& /*counter*/, int /*rounds*/)
        {
            executor->run_all();
        }

        /** @brief The executor the rounds run on. */
        std::shared_ptr<SeededExecutor> executor = std::make_shared<SeededExecutor>(3);

        /** @brief The clock the drive reads. */
        std::shared_ptr<ManualClock> clock = std::make_shared<ManualClock>();
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the contract on a drive a test steps. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_ExecutorDrive, TransportDriveContract, ::ExecutorDriveMaker, );
} // namespace TestSupport

/** @brief Tests that an ExecutorDrive reads the clock it was given, which the test advances. */
TEST(Small_ExecutorDrive, ReadsTheClockItWasGiven)
{
    const auto executor = std::make_shared<SeededExecutor>();
    const auto clock = std::make_shared<ManualClock>(40);
    ExecutorDrive drive(executor, clock);
    clock->advance(2);

    EXPECT_EQ(drive.now(), 42u);
}
