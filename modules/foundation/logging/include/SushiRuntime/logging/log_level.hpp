/**************************************************************************/
/* log_level.hpp                                                          */
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
 * @file log_level.hpp
 * @brief Declares LogLevel, the process-wide level and the gate a log macro reads.
 * @author Mustafa Garip
 * @see modules/foundation/logging/README.md
 */

#pragma once

#include <atomic>

#include <SushiRuntime/core/export.hpp>

#ifndef SR_ENABLE_LOGGING
    #define SR_ENABLE_LOGGING 1
#endif

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Orders the severities of a log record; a level admits itself and every lower one.
         */
        enum class LogLevel : int
        {
            Off   = 0, /**< Nothing is logged. */
            Error = 1, /**< An operation failed. */
            Warn  = 2, /**< The runtime recovered or fell back. */
            Info  = 3, /**< A lifecycle event. */
            Debug = 4  /**< Internal state at a decision point. */
        };

        #if !SR_ENABLE_LOGGING
            /** @brief Holds the highest level this translation unit compiles log calls for. */
            constexpr LogLevel K_LOG_LEVEL_CEILING = LogLevel::Off;
        #elif defined(NDEBUG)
            /** @brief Holds the highest level this translation unit compiles log calls for. */
            constexpr LogLevel K_LOG_LEVEL_CEILING = LogLevel::Info;
        #else
            /** @brief Holds the highest level this translation unit compiles log calls for. */
            constexpr LogLevel K_LOG_LEVEL_CEILING = LogLevel::Debug;
        #endif

        /**
         * @brief Sets the level of the whole process; records above it are dropped unformatted.
         * @see modules/foundation/logging/README.md
         */
        SUSHIRUNTIME_API void set_log_level(LogLevel level) noexcept;

        /**
         * @brief Returns the level of the whole process.
         * @return LogLevel::Warn, or what SR_LOG_LEVEL names, until set_log_level() is called.
         */
        [[nodiscard]] SUSHIRUNTIME_API LogLevel log_level() noexcept;

        /**
         * @brief Holds what the log macros read; nothing else calls into it.
         */
        namespace Detail
        {
            /** @brief Marks a level that SR_LOG_LEVEL has not been read for; above every level. */
            constexpr int K_LOG_LEVEL_UNRESOLVED = 127;

            /** @brief Holds the process level as an int, or K_LOG_LEVEL_UNRESOLVED. */
            SUSHIRUNTIME_API extern std::atomic<int> active_log_level;

            /**
             * @brief Returns whether a record of @p level is written, reading SR_LOG_LEVEL once.
             */
            [[nodiscard]] SUSHIRUNTIME_API bool is_log_enabled(LogLevel level) noexcept;
        } // namespace Detail
    } // namespace Core
} // namespace SushiRuntime
