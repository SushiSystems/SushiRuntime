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
 * @brief Names SushiLog's LogSink and the calls around it in SushiRuntime.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_sink.hpp>

#include <SushiRuntime/log/log_record.hpp>

namespace SushiRuntime
{
    /** @brief The callable that receives each record; SushiLog owns it. */
    using LogSink = SushiLog::LogSink;

    using SushiLog::log_forward_sink;
    using SushiLog::set_log_sink;
    using SushiLog::set_log_synchronous;
} // namespace SushiRuntime
