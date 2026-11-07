/**************************************************************************/
/* test_unsent_frame_log.cpp                                              */
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
 * @file test_unsent_frame_log.cpp
 * @brief Tests log_unsent_frame, the line the cluster monitor writes for a refused control frame.
 * @see tests/extension/cluster_monitor/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/cluster_monitor/unsent_frame_log.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/transport/transport.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that a queued frame logs nothing. */
TEST(Small_UnsentFrameLog, AQueuedFrameLogsNothing)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);

    Distributed::Detail::log_unsent_frame("PING", PeerID{7}, SendStatus::Queued);

    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that a refused frame logs one DEBUG with the frame, the peer and the reason. */
TEST(Small_UnsentFrameLog, ARefusedFrameLogsOneDebugLine)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);

    Distributed::Detail::log_unsent_frame("GOODBYE", PeerID{7}, SendStatus::UnknownPeer);

    ASSERT_EQ(capture.count_at(SushiLog::LogLevel::Debug), 1u);
    EXPECT_EQ(capture.count_in("cluster_monitor"), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "GOODBYE to worker 7"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "no open connection"));
}
