/**************************************************************************/
/* test_logging_defaults.cpp                                              */
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
 * @file test_logging_defaults.cpp
 * @brief Tests that Runtime::create() prints nothing at Warn and logs its devices at Info.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Holds one record a test sink received: its level and a copy of its line. */
    using SeenRecord = std::pair<Core::LogLevel, std::string>;

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
            auto runtime = Runtime::create();
            EXPECT_GE(runtime.advanced().device_count(), 1u);
        }
        captured.standard_error = testing::internal::GetCapturedStderr();
        captured.standard_output = testing::internal::GetCapturedStdout();
        return captured;
    }

    /** @brief Puts the logger in synchronous mode and restores the level and the sink. */
    class Integration_LoggingDefaults : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                saved_level_ = Core::log_level();
                Core::set_log_synchronous(true);
                Core::set_log_sink(nullptr);
            }

            void TearDown() override
            {
                Core::set_log_sink(nullptr);
                Core::set_log_level(saved_level_);
            }

        private:
            /// @brief The level the process had when the test began.
            Core::LogLevel saved_level_ = Core::LogLevel::Warn;
    };
}

/** @brief Tests that the default sink writes nothing while a Runtime is created at Warn. */
TEST_F(Integration_LoggingDefaults, CreateIsSilentAtTheDefaultLevel)
{
    Core::set_log_level(Core::LogLevel::Warn);

    const CapturedStreams captured = capture_streams_around_create();

    EXPECT_TRUE(captured.standard_output.empty()) << captured.standard_output;
    EXPECT_TRUE(captured.standard_error.empty()) << captured.standard_error;
}

/** @brief Tests that at Info the device line reaches the sink and neither stream. */
TEST_F(Integration_LoggingDefaults, DeviceLineArrivesThroughTheSinkAtInfo)
{
    std::vector<SeenRecord> records;
    Core::set_log_sink([&records](Core::LogLevel level, std::string_view line)
    {
        records.emplace_back(level, std::string(line));
    });
    Core::set_log_level(Core::LogLevel::Info);

    const CapturedStreams captured = capture_streams_around_create();
    Core::set_log_sink(nullptr);

    bool saw_device_line = false;
    for (const SeenRecord& record : records)
    {
        if (record.first == Core::LogLevel::Info &&
            record.second.find("Device: ") != std::string::npos)
            saw_device_line = true;
    }

    EXPECT_TRUE(saw_device_line) << records.size() << " records, none an Info device line";
    EXPECT_TRUE(captured.standard_output.empty()) << captured.standard_output;
    EXPECT_TRUE(captured.standard_error.empty()) << captured.standard_error;
}
