/**************************************************************************/
/* log_record.hpp                                                         */
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
 * @file log_record.hpp
 * @brief Declares LogRecord, the fields of one record a sink receives.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <cstdint>
#include <string_view>

#include <SushiRuntime/log/log_level.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds one record, each field apart; every view is valid only during the sink call.
     */
    struct LogRecord
    {
        /** @brief The level the record was logged at. */
        LogLevel level;
        /** @brief The project of the record's category. */
        std::string_view project;
        /** @brief The module of the record's category. */
        std::string_view module;
        /** @brief The file of the call, without its folders. */
        std::string_view file;
        /** @brief The line of the call. */
        std::uint32_t line;
        /** @brief The logger's number for the calling thread. */
        std::uint32_t thread;
        /** @brief The system clock time of the call, in nanoseconds since the Unix epoch. */
        std::int64_t time_ns;
        /** @brief The formatted message alone. */
        std::string_view message;
        /** @brief The whole line: time, level, category, thread, call site, then the message. */
        std::string_view text;
    };
} // namespace SushiRuntime
