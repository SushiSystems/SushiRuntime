/**************************************************************************/
/* test_socket_option_report.cpp                                          */
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
 * @file test_socket_option_report.cpp
 * @brief Tests report_no_delay_failure, the line a refused TCP_NODELAY option writes.
 * @see tests/extension/network_socket/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/network_socket/socket_option_report.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime;

/**
 * @brief Tests that one WARN names the peer and the option.
 *
 * The OS does not refuse TCP_NODELAY on loopback, so the test calls the report directly.
 */
TEST(Medium_Socket, LogsAFailedNoDelayOption)
{
    TestRunner::LogCapture capture;

    Distributed::Detail::report_no_delay_failure("127.0.0.1:9", 0);

    ASSERT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 1u);
    EXPECT_EQ(capture.count_in("network_socket"), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "TCP_NODELAY"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "127.0.0.1:9"));
}
