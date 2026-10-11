/**************************************************************************/
/* log_gate.hpp                                                           */
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
 * @file log_gate.hpp
 * @brief Declares the compile-time ceiling and the run-time gate the log macros read.
 * @author Mustafa Garip
 * @see modules/foundation/logging/README.md
 */

#pragma once

#include <atomic>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/log/log_level.hpp>

#ifndef SR_ENABLE_LOGGING
    #define SR_ENABLE_LOGGING 1
#endif

namespace SushiRuntime
{
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
} // namespace SushiRuntime
