/**************************************************************************/
/* logger.hpp                                                             */
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
 * @file logger.hpp
 * @brief Declares the process-wide logger and the SR_LOG macros.
 * @author Mustafa Garip
 * @see modules/foundation/logging/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>

#include <SushiRuntime/log/log_category.hpp>
#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/log/log_sink.hpp>

#include <SushiRuntime/logging/log_gate.hpp>

namespace SushiRuntime
{
    /**
     * @brief Hands one finished line to the sink, whatever the level of the process is.
     * @param line The text of the record, without a line break.
     * @see modules/foundation/logging/README.md
     */
    SUSHIRUNTIME_API void log_write(LogLevel level, std::string&& line);

    /**
     * @brief Holds what the macros of this header expand to.
     */
    namespace Detail
    {
        /**
         * @brief Returns @p format with each doubled brace written once.
         */
        inline std::string sr_format(std::string_view format)
        {
            std::string out;
            out.reserve(format.size());
            for (std::size_t i = 0; i < format.size(); ++i)
            {
                const char c = format[i];
                const bool doubled = (c == '{' || c == '}') && i + 1 < format.size() &&
                                     format[i + 1] == c;
                if (doubled)
                    ++i;
                out += c;
            }
            return out;
        }

        /**
         * @brief Returns @p format with each {} replaced by the next argument, in order.
         * @tparam T A type an output stream can write.
         */
        template <typename T, typename... Args>
        std::string sr_format(std::string_view format, const T& value, const Args&... args)
        {
            std::string out;
            for (std::size_t i = 0; i < format.size(); ++i)
            {
                const char c = format[i];
                const bool has_next = i + 1 < format.size();
                if ((c == '{' || c == '}') && has_next && format[i + 1] == c)
                {
                    out += c;
                    ++i;
                    continue;
                }
                if (c == '{' && has_next && format[i + 1] == '}')
                {
                    std::ostringstream os;
                    os << value;
                    out += os.str();
                    out += sr_format(format.substr(i + 2), args...);
                    return out;
                }
                out += c;
            }
            return out;
        }

        /**
         * @brief Prefixes @p message with the time, the level, the category, the thread and the
         *        call site, then writes it.
         */
        SUSHIRUNTIME_API void emit_record(LogLevel level, LogCategory category, const char* file,
                                          int line, std::string&& message);

        /**
         * @brief Formats one record and writes it; a log macro calls it past its gate.
         */
        template <typename... Args>
        void log_record(LogLevel level, LogCategory category, const char* file, int line,
                        std::string_view format, const Args&... args)
        {
            emit_record(level, category, file, line, sr_format(format, args...));
        }
    } // namespace Detail
} // namespace SushiRuntime

/**
 * @brief Logs one record at @p level in @p category when the floor and the category's level
 *        both admit it.
 *
 * A call below K_LOG_LEVEL_FLOOR compiles to nothing. A call below the category's level costs
 * one relaxed load and a branch, and its arguments are not evaluated.
 */
#define SR_LOG_AT(level, category, ...)                                                          \
    do                                                                                           \
    {                                                                                            \
        if constexpr ((level) >= SushiRuntime::K_LOG_LEVEL_FLOOR)                                \
        {                                                                                        \
            static std::atomic<int>& sr_log_level_slot_ =                                        \
                SushiRuntime::Detail::log_level_slot(category);                                  \
            if (static_cast<int>(level) >= sr_log_level_slot_.load(std::memory_order_relaxed))   \
            {                                                                                    \
                SushiRuntime::Detail::log_record((level), (category), __FILE__, __LINE__,        \
                                                 __VA_ARGS__);                                   \
            }                                                                                    \
        }                                                                                        \
    } while (0)

/** @brief Logs step-by-step flow in a category; each {} takes the next argument. */
#define SR_LOG_TRACE(category, ...) SR_LOG_AT(SushiRuntime::LogLevel::Trace, category, __VA_ARGS__)

/** @brief Logs internal state in a category; each {} takes the next argument. */
#define SR_LOG_DEBUG(category, ...) SR_LOG_AT(SushiRuntime::LogLevel::Debug, category, __VA_ARGS__)

/** @brief Logs a lifecycle event in a category; each {} takes the next argument. */
#define SR_LOG_INFO(category, ...) SR_LOG_AT(SushiRuntime::LogLevel::Info, category, __VA_ARGS__)

/** @brief Logs a recovery or a fallback in a category; each {} takes the next argument. */
#define SR_LOG_WARN(category, ...) SR_LOG_AT(SushiRuntime::LogLevel::Warn, category, __VA_ARGS__)

/** @brief Logs a failed operation in a category; each {} takes the next argument. */
#define SR_LOG_ERROR(category, ...) SR_LOG_AT(SushiRuntime::LogLevel::Error, category, __VA_ARGS__)

/**
 * @brief Logs why the process cannot continue, after every queued record, and flushes; the
 *        caller then ends the process.
 */
#define SR_LOG_FATAL(category, ...) SR_LOG_AT(SushiRuntime::LogLevel::Fatal, category, __VA_ARGS__)

/** @brief Forwards to set_log_synchronous(); tests pass true to read records in order. */
#define SR_LOGGER_SET_SYNC_MODE(mode) SushiRuntime::set_log_synchronous(mode)
