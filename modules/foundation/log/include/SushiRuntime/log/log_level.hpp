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
 * @brief Declares LogLevel and the calls that set and read the level of the process.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
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

    /**
     * @brief Sets the level of the whole process; records above it are dropped unformatted.
     * @see modules/foundation/log/README.md
     */
    SUSHIRUNTIME_API void set_log_level(LogLevel level) noexcept;

    /**
     * @brief Returns the level of the whole process.
     * @return LogLevel::Warn, or what SR_LOG_LEVEL names, until set_log_level() is called.
     */
    [[nodiscard]] SUSHIRUNTIME_API LogLevel log_level() noexcept;
} // namespace SushiRuntime
