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
 * @brief Declares LogSink, the callable that receives each record, and the calls around it.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiRuntime/core/delegate.hpp>
#include <SushiRuntime/core/export.hpp>

#include <SushiRuntime/log/log_record.hpp>

namespace SushiRuntime
{
    /**
     * @brief Receives one record. The record is valid only during the call; a sink does not
     *        log and does not call set_log_sink().
     */
    using LogSink = Delegate<void(const LogRecord& record), 48>;

    /**
     * @brief Replaces the sink of the whole process and returns once the old one is idle.
     * @param sink An empty sink restores the default, which writes every line to stderr.
     * @see modules/foundation/log/README.md
     */
    SUSHIRUNTIME_API void set_log_sink(LogSink sink);

    /**
     * @brief Chooses whether a record reaches the sink before its log call returns.
     * @param synchronous True first delivers every record still queued, then makes each
     *        log call run the sink itself; false hands records to the logger's thread.
     * @see modules/foundation/log/README.md
     */
    SUSHIRUNTIME_API void set_log_synchronous(bool synchronous);

    /**
     * @brief Returns a sink that enters another logger's records into this logger's queue.
     *
     * A forwarded record passes no level gate here. It keeps its time and call site, takes
     * this logger's number for the calling thread and gets its line written here.
     * @see modules/foundation/log/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API LogSink log_forward_sink();
} // namespace SushiRuntime
