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
 * @brief Declares the process-wide logger, the SR_LOG macros and SR_THROW_IF.
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

#include <SushiRuntime/logging/log_level.hpp>
#include <SushiRuntime/logging/log_sink.hpp>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Hands one finished line to the sink, whatever the level of the process is.
         * @param line The text of the record, without a line break.
         * @see modules/foundation/logging/README.md
         */
        SUSHIRUNTIME_API void log_write(LogLevel level, std::string&& line);

        /**
         * @brief Chooses whether a record reaches the sink before its log call returns.
         * @param synchronous True first delivers every record still queued, then makes each
         *        log call run the sink itself; false hands records to the logger's thread.
         */
        SUSHIRUNTIME_API void set_log_synchronous(bool synchronous);

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
             * @brief Prefixes @p message with the time, the level and the call site, then
             *        writes it.
             */
            SUSHIRUNTIME_API void emit_record(LogLevel level, const char* file, int line,
                                              const char* function, std::string&& message);

            /**
             * @brief Formats one record and writes it; a log macro calls it past its gate.
             */
            template <typename... Args>
            void log_record(LogLevel level, const char* file, int line, const char* function,
                            std::string_view format, const Args&... args)
            {
                emit_record(level, file, line, function, sr_format(format, args...));
            }
        } // namespace Detail
    } // namespace Core
} // namespace SushiRuntime

/**
 * @brief Logs one record at @p level when the ceiling and the process level both admit it.
 *
 * A call above K_LOG_LEVEL_CEILING compiles to nothing. A call above the process level costs
 * one relaxed load and a branch, and its arguments are not evaluated.
 */
#define SR_LOG_AT(level, ...)                                                                \
    do                                                                                       \
    {                                                                                        \
        if constexpr (SushiRuntime::Core::K_LOG_LEVEL_CEILING >= (level))                    \
        {                                                                                    \
            if (SushiRuntime::Core::Detail::active_log_level.load(                           \
                    std::memory_order_relaxed) >= static_cast<int>(level) &&                 \
                SushiRuntime::Core::Detail::is_log_enabled(level))                           \
            {                                                                                \
                SushiRuntime::Core::Detail::log_record((level), __FILE__, __LINE__,          \
                                                       __func__, __VA_ARGS__);               \
            }                                                                                \
        }                                                                                    \
    } while (0)

/** @brief Logs a message at error level; each {} takes the next argument. */
#define SR_LOG_ERROR(...) SR_LOG_AT(SushiRuntime::Core::LogLevel::Error, __VA_ARGS__)

/** @brief Logs a message at warning level; each {} takes the next argument. */
#define SR_LOG_WARN(...) SR_LOG_AT(SushiRuntime::Core::LogLevel::Warn, __VA_ARGS__)

/** @brief Logs a message at info level; each {} takes the next argument. */
#define SR_LOG_INFO(...) SR_LOG_AT(SushiRuntime::Core::LogLevel::Info, __VA_ARGS__)

/** @brief Logs a message at debug level; each {} takes the next argument. */
#define SR_LOG_DEBUG(...) SR_LOG_AT(SushiRuntime::Core::LogLevel::Debug, __VA_ARGS__)

/** @brief Forwards to Core::set_log_synchronous(); tests pass true to read records in order. */
#define SR_LOGGER_SET_SYNC_MODE(mode) SushiRuntime::Core::set_log_synchronous(mode)

/**
 * @brief Logs the message at error level and throws it when @p condition holds.
 *
 * Throws Core::SushiException with Errc::invalid_state. The arguments after the condition are
 * a format string and its values, and are evaluated only when the condition holds.
 */
#define SR_THROW_IF(condition, ...)                                                          \
    do                                                                                       \
    {                                                                                        \
        if (condition)                                                                       \
        {                                                                                    \
            std::string msg_ = SushiRuntime::Core::Detail::sr_format(__VA_ARGS__);           \
            SR_LOG_ERROR("Runtime exception: {}", msg_);                                     \
            throw SushiRuntime::Core::SushiException(SushiRuntime::Core::Error{              \
                SushiRuntime::Core::Errc::invalid_state, std::move(msg_)});                  \
        }                                                                                    \
    } while (0)
