/**************************************************************************/
/* test_stop_signal.cpp                                                   */
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
 * @file test_stop_signal.cpp
 * @brief Tests StopSignal: the flag starts clear, request() sets it and a refused handler is logged.
 * @see tests/extension/process_environment/README.md
 * @author Mustafa Garip
 */

#include <csignal>

#include <gtest/gtest.h>

#include <SushiRuntime/process_environment/stop_signal.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Accepts every installation without touching the process's handlers. */
    StopSignal::Handler accept_every_signal(int, StopSignal::Handler)
    {
        return SIG_DFL;
    }

    /** @brief Refuses every installation, as a C runtime that cannot take a handler does. */
    StopSignal::Handler refuse_every_signal(int, StopSignal::Handler)
    {
        return SIG_ERR;
    }
} // namespace

/** @brief Tests that request() sets a flag that starts clear. */
TEST(Small_StopSignal, RequestSetsTheFlag)
{
    StopSignal stop_signal(&accept_every_signal);
    ASSERT_FALSE(stop_signal.is_requested());

    stop_signal.request();

    EXPECT_TRUE(stop_signal.is_requested());
}

/** @brief Tests that an object made after a request starts with the flag clear. */
TEST(Small_StopSignal, ANewObjectAfterARequestStartsClear)
{
    {
        StopSignal first(&accept_every_signal);
        first.request();
    }

    const StopSignal next(&accept_every_signal);

    EXPECT_FALSE(next.is_requested());
}

/** @brief Tests that a refused installation is logged once per signal as a WARN. */
TEST(Small_StopSignal, LogsAHandlerThatCouldNotBeInstalled)
{
    SushiRuntime::TestRunner::LogCapture capture;

    const StopSignal stop_signal(&refuse_every_signal);

    EXPECT_GE(capture.count_at(SushiLog::LogLevel::Warn), 2u);
    EXPECT_EQ(capture.count_in("process_environment"),
              capture.count_at(SushiLog::LogLevel::Warn));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "could not be installed"));
}

/** @brief Tests that the flag still works after every installation was refused. */
TEST(Small_StopSignal, TheFlagStillWorksAfterARefusedInstallation)
{
    StopSignal stop_signal(&refuse_every_signal);
    ASSERT_FALSE(stop_signal.is_requested());

    stop_signal.request();

    EXPECT_TRUE(stop_signal.is_requested());
}
