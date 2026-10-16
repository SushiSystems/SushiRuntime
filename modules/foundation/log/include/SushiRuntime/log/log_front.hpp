/**************************************************************************/
/* log_front.hpp                                                          */
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
 * @file log_front.hpp
 * @brief Declares SR_LOG_FRONT_AT and log_level_floor, for a project with log macros of its own.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include <SushiRuntime/core/export.hpp>

#include <SushiRuntime/log/log_category.hpp>
#include <SushiRuntime/log/log_format.hpp>
#include <SushiRuntime/log/log_level.hpp>

namespace SushiRuntime
{
    /**
     * @brief Returns the least severe level a translation unit compiles log calls for.
     *
     * Error when @p enabled is false, Debug under NDEBUG, Trace otherwise. It has internal
     * linkage, so each translation unit reads its own NDEBUG.
     */
    [[nodiscard]] static constexpr LogLevel log_level_floor(bool enabled) noexcept
    {
        if (!enabled)
            return LogLevel::Error;
        #ifdef NDEBUG
            return LogLevel::Debug;
        #else
            return LogLevel::Trace;
        #endif
    }

    namespace Detail
    {
        /**
         * @brief Returns the level of @p category as an int, reading its project's variable on
         *        the first use of the project.
         *
         * A call site keeps the reference in a static, so each later call costs one relaxed
         * load. The reference stays valid for the life of the process.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::atomic<int>& log_level_slot(
            LogCategory category) noexcept;

        /**
         * @brief Hands one formatted message to the logger, which stamps its time and thread.
         */
        SUSHIRUNTIME_API void emit_record(LogLevel level, LogCategory category, const char* file,
                                          int line, std::string&& message);

        /**
         * @brief Formats one record and hands it to the logger; a log macro calls it past its
         *        gates.
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
 * @brief Logs one record at @p level in @p category when @p floor and the category's level
 *        both admit it.
 *
 * A front defines its macros on it, passing its own floor. A call below @p floor compiles to
 * nothing; one below the category's level costs one relaxed load and a branch.
 */
#define SR_LOG_FRONT_AT(floor, level, category, ...)                                             \
    do                                                                                           \
    {                                                                                            \
        if constexpr ((level) >= (floor))                                                        \
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
