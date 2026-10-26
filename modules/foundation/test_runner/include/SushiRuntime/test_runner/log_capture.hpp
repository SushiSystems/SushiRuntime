/**************************************************************************/
/* log_capture.hpp                                                        */
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
 * @file log_capture.hpp
 * @brief Declares LogCapture, which collects the records a test makes and restores the logger.
 * @author Mustafa Garip
 * @see modules/foundation/test_runner/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_project.hpp>
#include <SushiLog/log/log_record.hpp>
#include <SushiLog/log/log_sink.hpp>

#include <SushiRuntime/log/sushiruntime_log_project.hpp>

namespace SushiRuntime::TestRunner
{
    /** @brief Holds a copy of one record; the views of a LogRecord die with the sink call. */
    struct CapturedRecord
    {
        SushiLog::LogLevel level; /**< The level the record was logged at. */
        std::string project;      /**< The project of the record's category. */
        std::string module;       /**< The module of the record's category. */
        std::string message;      /**< The formatted message alone. */
        std::string text;         /**< The whole line of the record. */
    };

    /**
     * @brief Routes SushiLog's records into a vector while it lives.
     *
     * The constructor makes the logger synchronous, sets one project's level and installs the
     * sink. The destructor restores the default sink and the level the project had. One capture
     * lives at a time: the sink is a process-wide setting.
     */
    class LogCapture final
    {
        public:
            /**
             * @brief Starts capturing records of @p project that pass @p level.
             * @param project The project whose level is set; defaults to the runtime's.
             * @param level The lowest level that reaches the capture.
             */
            explicit LogCapture(SushiLog::LogProject project = K_SUSHIRUNTIME_LOG_PROJECT,
                                SushiLog::LogLevel level = SushiLog::LogLevel::Warn)
                : project_(project), saved_level_(SushiLog::log_level(project))
            {
                SushiLog::set_log_synchronous(true);
                SushiLog::set_log_level(project_, level);
                SushiLog::set_log_sink(
                    [this](const SushiLog::LogRecord& record)
                    {
                        append(record);
                    });
            }

            /** @brief Restores the default sink and the level the project had. */
            ~LogCapture()
            {
                SushiLog::set_log_sink(nullptr);
                SushiLog::set_log_level(project_, saved_level_);
            }

            LogCapture(const LogCapture&) = delete;
            LogCapture& operator=(const LogCapture&) = delete;
            LogCapture(LogCapture&&) = delete;
            LogCapture& operator=(LogCapture&&) = delete;

            /** @brief Returns a copy of every record seen so far, in order. */
            [[nodiscard]] std::vector<CapturedRecord> records() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return records_;
            }

            /** @brief Returns how many records at exactly @p level were seen. */
            [[nodiscard]] std::size_t count_at(SushiLog::LogLevel level) const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                std::size_t count = 0;
                for (const CapturedRecord& record : records_)
                {
                    if (record.level == level)
                        ++count;
                }
                return count;
            }

            /** @brief Returns how many records in module @p module were seen. */
            [[nodiscard]] std::size_t count_in(const std::string& module) const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                std::size_t count = 0;
                for (const CapturedRecord& record : records_)
                {
                    if (record.module == module)
                        ++count;
                }
                return count;
            }

            /** @brief Returns whether a record at @p level has a message containing @p text. */
            [[nodiscard]] bool has_message(SushiLog::LogLevel level, const std::string& text) const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const CapturedRecord& record : records_)
                {
                    if (record.level == level && record.message.find(text) != std::string::npos)
                        return true;
                }
                return false;
            }

            /** @brief Forgets the records seen so far. */
            void clear()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                records_.clear();
            }

        private:
            /** @brief Copies @p record into the vector; runs on the logging thread. */
            void append(const SushiLog::LogRecord& record)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                records_.push_back(CapturedRecord{record.level,
                                                  std::string(record.project),
                                                  std::string(record.module),
                                                  std::string(record.message),
                                                  std::string(record.text)});
            }

            /// @brief The project whose level the capture set.
            SushiLog::LogProject project_;
            /// @brief The level the project had before the capture.
            SushiLog::LogLevel saved_level_;
            /// @brief Guards records_; the sink may run on any thread.
            mutable std::mutex mutex_;
            /// @brief The records seen so far.
            std::vector<CapturedRecord> records_;
    };
} // namespace SushiRuntime::TestRunner
