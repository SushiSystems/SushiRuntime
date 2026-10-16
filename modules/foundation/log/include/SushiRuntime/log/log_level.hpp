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
 * @brief Declares LogLevel, the severity of a log record.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Orders the severities of a log record, least severe first; a record passes when
     *        its level is at or above the level set for its category.
     *
     * The numbers are part of the C interface in log_c_sink.h and never change.
     */
    enum class LogLevel : std::uint8_t
    {
        Trace = 0, /**< Step-by-step flow, for chasing one bug. */
        Debug = 1, /**< Internal state at a decision point. */
        Info  = 2, /**< A lifecycle event. */
        Warn  = 3, /**< The runtime recovered or fell back. */
        Error = 4, /**< An operation failed. */
        Fatal = 5, /**< The process cannot continue. */
        Off   = 6  /**< A setting that passes nothing; never the level of a record. */
    };
} // namespace SushiRuntime
