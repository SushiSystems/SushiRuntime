/**************************************************************************/
/* log_sink.hpp                                                           */
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
 * @file log_sink.hpp
 * @brief Declares LogSink, the callable that receives each log line, and its setter.
 * @author Mustafa Garip
 * @see modules/foundation/logging/README.md
 */

#pragma once

#include <string_view>

#include <SushiRuntime/core/delegate.hpp>
#include <SushiRuntime/core/export.hpp>

#include <SushiRuntime/logging/log_level.hpp>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Receives one record: its level and its line, which carries no line break.
         *
         * The line is valid only during the call. A sink does not log and does not call
         * set_log_sink().
         */
        using LogSink = Delegate<void(LogLevel level, std::string_view line), 48>;

        /**
         * @brief Replaces the sink of the whole process and returns once the old one is idle.
         * @param sink An empty sink restores the default, which writes every line to stderr.
         * @see modules/foundation/logging/README.md
         */
        SUSHIRUNTIME_API void set_log_sink(LogSink sink);
    } // namespace Core
} // namespace SushiRuntime
