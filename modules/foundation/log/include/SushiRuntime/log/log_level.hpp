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
 * @brief Declares LogLevel and the calls that set and read the level of every category.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    /**
     * @brief Orders the severities of a log record, least severe first; a record passes when
     *        its level is at or above the level set for its category.
     */
    enum class LogLevel : int
    {
        Trace = 0, /**< Step-by-step flow, for chasing one bug. */
        Debug = 1, /**< Internal state at a decision point. */
        Info  = 2, /**< A lifecycle event. */
        Warn  = 3, /**< The runtime recovered or fell back. */
        Error = 4, /**< An operation failed. */
        Fatal = 5, /**< The process cannot continue. */
        Off   = 6  /**< A setting that passes nothing; never the level of a record. */
    };

    /**
     * @brief Sets the level of every category; records below it are dropped unformatted.
     * @see modules/foundation/log/README.md
     */
    SUSHIRUNTIME_API void set_log_level(LogLevel level) noexcept;

    /**
     * @brief Returns the level a category takes when nothing sets its own.
     * @return LogLevel::Warn, or the first level SR_LOG_LEVEL names, until set_log_level() is
     *         called.
     */
    [[nodiscard]] SUSHIRUNTIME_API LogLevel log_level() noexcept;
} // namespace SushiRuntime
