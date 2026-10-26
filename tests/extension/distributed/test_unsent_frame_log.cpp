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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */

/**
 * @file test_unsent_frame_log.cpp
 * @brief Tests log_unsent_frame, the line the cluster monitor writes for a refused control frame.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/distributed/unsent_frame_log.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/transport/transport.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that a queued frame logs nothing. */
TEST(Small_UnsentFrameLog, AQueuedFrameLogsNothing)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);

    Detail::log_unsent_frame("PING", PeerID{7}, SendStatus::Queued);

    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that a refused frame logs one DEBUG with the frame, the peer and the reason. */
TEST(Small_UnsentFrameLog, ARefusedFrameLogsOneDebugLine)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);

    Detail::log_unsent_frame("GOODBYE", PeerID{7}, SendStatus::UnknownPeer);

    ASSERT_EQ(capture.count_at(SushiLog::LogLevel::Debug), 1u);
    EXPECT_EQ(capture.count_in("distributed"), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "GOODBYE to worker 7"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "no open connection"));
}
