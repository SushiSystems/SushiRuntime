/**************************************************************************/
/* test_logging.cpp                                                       */
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
 * @file test_logging.cpp
 * @brief Tests the logging controls of the surface and what a default Runtime logs.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_category.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_record.hpp>
#include <SushiLog/log/log_sink.hpp>

#include <SushiRuntime/api/runtime/logging.hpp>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Tests that the logging controls are reachable from the surface's logging header alone. */
namespace LoggingNamesTests
{
    static_assert(std::is_same<decltype(&SushiLog::set_log_synchronous), void (*)(bool)>::value,
                  "SushiLog::set_log_synchronous(bool) must be declared by api/runtime/logging.hpp");
    static_assert(std::is_same<decltype(&SushiLog::set_log_sink),
                               void (*)(SushiLog::LogSink)>::value,
                  "SushiLog::set_log_sink(SushiLog::LogSink) must be declared by api/runtime/logging.hpp");

    /** @brief Tests that a program switches delivery, the sink and the level with this header. */
    TEST(Medium_LoggingNames, TheControlsAreCallableFromTheSurfaceHeader)
    {
        // SushiLog sets a project only once it has seen one of its categories.
        (void)SushiLog::log_level(
            SushiLog::make_log_category(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT, "device"));
        const SushiLog::LogLevel saved =
            SushiLog::log_level(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT);
        std::string seen;

        SushiLog::set_log_synchronous(true);
        SushiLog::set_log_sink([&seen](const SushiLog::LogRecord& record)
        {
            seen.assign(record.text);
        });
        SushiLog::set_log_level(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                    SushiLog::LogLevel::Error);
        EXPECT_EQ(SushiLog::log_level(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT),
                  SushiLog::LogLevel::Error);

        // No record is logged here, so the sink stays unused.
        EXPECT_TRUE(seen.empty());

        SushiLog::set_log_sink(SushiLog::LogSink());
        SushiLog::set_log_synchronous(false);
        SushiLog::set_log_level(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT, saved);
    }
} // namespace LoggingNamesTests

/** @brief Tests that Runtime::create() prints nothing at Warn and logs its devices at Info. */
namespace LoggingDefaultsTests
{
    namespace
    {
        /** @brief Holds one record a test sink received: its level and a copy of its line. */
        using SeenRecord = std::pair<SushiLog::LogLevel, std::string>;

        /** @brief Holds the two process streams as captured around one call. */
        struct CapturedStreams
        {
            std::string standard_output; /**< What reached stdout. */
            std::string standard_error;  /**< What reached stderr. */
        };

        /** @brief Creates and destroys a default Runtime and returns what the streams received. */
        CapturedStreams capture_streams_around_create()
        {
            CapturedStreams captured;
            testing::internal::CaptureStdout();
            testing::internal::CaptureStderr();
            {
                auto runtime = Runtime::create().value();
                EXPECT_GE(runtime.advanced().device_count(), 1u);
            }
            captured.standard_error = testing::internal::GetCapturedStderr();
            captured.standard_output = testing::internal::GetCapturedStdout();
            return captured;
        }

        /** @brief Tells whether a line starts the way the logger writes one: a time, then a level. */
        bool is_logger_line(const std::string& line)
        {
            static const std::regex prefix(R"(^\[\d{2}:\d{2}:\d{2}\.\d{3}\] \[)");
            return std::regex_search(line, prefix);
        }

        /** @brief Returns the lines of @p text the logger wrote, dropping any other output. */
        std::string logger_lines_only(const std::string& text)
        {
            std::string kept;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
            {
                if (is_logger_line(line))
                    kept += line + '\n';
            }
            return kept;
        }

        /** @brief Puts the logger in synchronous mode and restores the level and the sink. */
        class Medium_LoggingDefaults : public ::testing::Test
        {
            protected:
                void SetUp() override
                {
                    // SushiLog sets a project only once it has seen one of its categories.
                    (void)SushiLog::log_level(
                        SushiLog::make_log_category(K_SUSHIRUNTIME_LOG_PROJECT, "device"));
                    saved_level_ = SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT);
                    SushiLog::set_log_synchronous(true);
                    SushiLog::set_log_sink(nullptr);
                }

                void TearDown() override
                {
                    SushiLog::set_log_sink(nullptr);
                    SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, saved_level_);
                }

            private:
                /// @brief The level the process had when the test began.
                SushiLog::LogLevel saved_level_ = SushiLog::LogLevel::Warn;
        };
    }

    /**
     * @brief Tests that the default sink writes no logger line while a Runtime is created at Warn.
     *
     * Output that is not the logger's, such as a SYCL device sanitizer banner printed under
     * ThreadSanitizer, is not the logger's to silence and is ignored on stderr.
     */
    TEST_F(Medium_LoggingDefaults, CreateIsSilentAtTheDefaultLevel)
    {
        SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Warn);

        const CapturedStreams captured = capture_streams_around_create();

        EXPECT_TRUE(captured.standard_output.empty()) << captured.standard_output;
        EXPECT_TRUE(logger_lines_only(captured.standard_error).empty()) << captured.standard_error;
    }

    /** @brief Tests that at Info the device line reaches the sink and no logger line reaches stderr. */
    TEST_F(Medium_LoggingDefaults, DeviceLineArrivesThroughTheSinkAtInfo)
    {
        std::vector<SeenRecord> records;
        SushiLog::set_log_sink([&records](const SushiLog::LogRecord& record)
        {
            records.emplace_back(record.level, std::string(record.text));
        });
        SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Info);

        const CapturedStreams captured = capture_streams_around_create();
        SushiLog::set_log_sink(nullptr);

        bool saw_device_line = false;
        for (const SeenRecord& record : records)
        {
            if (record.first == SushiLog::LogLevel::Info &&
                record.second.find("Device: ") != std::string::npos)
                saw_device_line = true;
        }

        EXPECT_TRUE(saw_device_line) << records.size() << " records, none an Info device line";
        EXPECT_TRUE(captured.standard_output.empty()) << captured.standard_output;
        EXPECT_TRUE(logger_lines_only(captured.standard_error).empty()) << captured.standard_error;
    }
} // namespace LoggingDefaultsTests
