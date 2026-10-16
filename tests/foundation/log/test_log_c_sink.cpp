/**************************************************************************/
/* test_log_c_sink.cpp                                                    */
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
/**************************************************************************/

/**
 * @file test_log_c_sink.cpp
 * @brief Tests sr_set_log_sink and sr_set_log_synchronous, which forward to SushiLog.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/log/log_c_sink.h>

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiLog/log/log_front.hpp>

#include <SushiRuntime/log/log_category.hpp>
#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/log/log_project.hpp>
#include <SushiRuntime/log/log_sink.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the project of the records these tests log. */
    constexpr LogProject K_C_SINK_TEST_LOG_PROJECT{"c_sink_test", "SR_C_SINK_TEST_LOG_LEVEL"};

    /** @brief Names the category of those records. */
    constexpr LogCategory K_C_SINK_TEST_LOG_CATEGORY =
        make_log_category(K_C_SINK_TEST_LOG_PROJECT, "host");

    /** @brief Returns the string a C record field points at. */
    std::string text_of(const char* data, size_t size)
    {
        return std::string(data, size);
    }

    /** @brief Holds what the C test sink received. */
    struct Received
    {
        std::vector<SushiLogRecord> records; /**< Each record; its pointers die with the call. */
        std::vector<std::string> copies;     /**< project|module|file|message|text of each. */
    };

    /** @brief Copies every field of @p record into the Received behind @p user. */
    void receive(const SushiLogRecord* record, void* user)
    {
        Received& received = *static_cast<Received*>(user);
        received.records.push_back(*record);
        received.copies.push_back(text_of(record->project, record->project_size) + "|" +
                                  text_of(record->module, record->module_size) + "|" +
                                  text_of(record->file, record->file_size) + "|" +
                                  text_of(record->message, record->message_size) + "|" +
                                  text_of(record->text, record->text_size));
    }

    /** @brief Logs one WARN through the C sink in synchronous mode and returns what it got. */
    Received log_through_the_c_sink()
    {
        Received received;
        sr_set_log_synchronous(1);
        sr_set_log_sink(&receive, &received);
        SL_LOG_FRONT_AT(SushiLog::log_level_ceiling(true), LogLevel::Warn,
                        K_C_SINK_TEST_LOG_CATEGORY, "host saw {}", 5);
        sr_set_log_sink(nullptr, nullptr);
        return received;
    }
}

/** @brief Tests that the sr_ C calls install a C sink that receives every field. */
TEST(Medium_LogCSink, TheCSinkReceivesEveryField)
{
    const Received received = log_through_the_c_sink();

    ASSERT_EQ(received.records.size(), 1u);
    const SushiLogRecord& record = received.records[0];
    EXPECT_EQ(record.size, sizeof(SushiLogRecord));
    EXPECT_EQ(record.level, static_cast<uint8_t>(LogLevel::Warn));
    EXPECT_GT(record.line, 0u);
    EXPECT_GT(record.thread, 0u);
    EXPECT_GT(record.time_ns, 0);
    const std::string& copy = received.copies[0];
    EXPECT_EQ(copy.rfind("c_sink_test|host|test_log_c_sink.cpp|host saw 5|[", 0), 0u) << copy;
    EXPECT_NE(copy.find("[WARN] [c_sink_test.host] [t" + std::to_string(record.thread) + "]"),
              std::string::npos)
        << copy;
}

