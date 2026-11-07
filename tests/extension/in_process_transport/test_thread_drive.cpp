/**************************************************************************/
/* test_thread_drive.cpp                                                  */
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
 * @file test_thread_drive.cpp
 * @brief Tests the TransportDrive contract on ThreadDrive, the boundary ExecutorDrive stands in for.
 * @see tests/extension/in_process_transport/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/in_process_transport/thread_drive.hpp>

#include "support/transport_drive_contract.hpp"

using SushiRuntime::Distributed::ThreadDrive;
using SushiRuntime::Distributed::TransportDrive;

namespace
{
    /** @brief Makes a drive with a thread, and waits on the counter for the rounds asked for. */
    struct ThreadDriveMaker
    {
        /** @brief Returns a drive that starts its thread at start(). */
        std::unique_ptr<TransportDrive> make() { return std::make_unique<ThreadDrive>(); }

        /** @brief Blocks until the rounds asked for have run. */
        void settle(TestSupport::RoundCounter& counter, int rounds) { counter.wait_for(rounds); }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the contract on a thread, the boundary ExecutorDrive stands in for. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_ThreadDrive, TransportDriveContract, ::ThreadDriveMaker, );
} // namespace TestSupport
