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
 * @brief Declares the compile-time floor and the run-time gate the log macros read.
 * @author Mustafa Garip
 * @see modules/foundation/logging/README.md
 */

#pragma once

#include <atomic>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/log/log_category.hpp>
#include <SushiRuntime/log/log_level.hpp>

#ifndef SR_ENABLE_LOGGING
    #define SR_ENABLE_LOGGING 1
#endif

namespace SushiRuntime
{
    #if !SR_ENABLE_LOGGING
        /** @brief Holds the least severe level this translation unit compiles log calls for. */
        constexpr LogLevel K_LOG_LEVEL_FLOOR = LogLevel::Error;
    #elif defined(NDEBUG)
        /** @brief Holds the least severe level this translation unit compiles log calls for. */
        constexpr LogLevel K_LOG_LEVEL_FLOOR = LogLevel::Debug;
    #else
        /** @brief Holds the least severe level this translation unit compiles log calls for. */
        constexpr LogLevel K_LOG_LEVEL_FLOOR = LogLevel::Trace;
    #endif

    /**
     * @brief Holds what the log macros read; nothing else calls into it.
     */
    namespace Detail
    {
        /**
         * @brief Returns the level of @p category as an int, reading SR_LOG_LEVEL on the first
         *        call of the process.
         *
         * A call site keeps the reference in a static, so each later call costs one relaxed
         * load. The reference stays valid for the life of the process.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::atomic<int>& log_level_slot(
            LogCategory category) noexcept;

        /**
         * @brief Returns whether a record of @p level in @p category passes the run-time gate.
         */
        [[nodiscard]] inline bool is_log_enabled(LogCategory category, LogLevel level) noexcept
        {
            return static_cast<int>(level) >=
                   log_level_slot(category).load(std::memory_order_relaxed);
        }
    } // namespace Detail
} // namespace SushiRuntime
