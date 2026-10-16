/**************************************************************************/
/* log_c_sink.h                                                           */
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
 * @file log_c_sink.h
 * @brief Declares the C interface through which a host outside C++ receives every record.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <SushiRuntime/core/export.hpp>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Holds one record for a C sink; no string is NUL-terminated.
 *
 * A later version only adds fields at the end. A host reads only the fields that both its
 * own sizeof(SushiLogRecord) and @c size cover.
 */
typedef struct SushiLogRecord
{
    /** @brief sizeof(SushiLogRecord) of the logger that filled the record. */
    uint32_t size;
    /** @brief The level, numbered as SushiRuntime::LogLevel: Trace 0 to Fatal 5. */
    uint8_t level;
    /** @brief The project of the record's category. */
    const char* project;
    /** @brief The length of @c project. */
    size_t project_size;
    /** @brief The module of the record's category. */
    const char* module;
    /** @brief The length of @c module. */
    size_t module_size;
    /** @brief The file of the call, without its folders. */
    const char* file;
    /** @brief The length of @c file. */
    size_t file_size;
    /** @brief The line of the call. */
    uint32_t line;
    /** @brief The logger's number for the calling thread. */
    uint32_t thread;
    /** @brief The system clock time of the call, in nanoseconds since the Unix epoch. */
    int64_t time_ns;
    /** @brief The formatted message alone. */
    const char* message;
    /** @brief The length of @c message. */
    size_t message_size;
    /** @brief The whole line of the record. */
    const char* text;
    /** @brief The length of @c text. */
    size_t text_size;
} SushiLogRecord;

/**
 * @brief Receives one record and the pointer given to sr_set_log_sink(); it must not throw
 *        or unwind into the logger.
 */
typedef void (*SushiLogSinkFunction)(const SushiLogRecord* record, void* user);

/**
 * @brief Replaces the sink of the process with @p function, called with @p user.
 * @param function A null function restores the default, which writes to stderr.
 */
SUSHIRUNTIME_API void sr_set_log_sink(SushiLogSinkFunction function, void* user);

/**
 * @brief Chooses whether a record reaches the sink before its log call returns.
 * @param synchronous Nonzero waits for the queue, then delivers on the calling thread.
 */
SUSHIRUNTIME_API void sr_set_log_synchronous(int synchronous);

#ifdef __cplusplus
}
#endif
