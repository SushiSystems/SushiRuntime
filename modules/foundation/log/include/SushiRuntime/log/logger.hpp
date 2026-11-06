/**************************************************************************/
/* logger.hpp                                                             */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file logger.hpp
 * @brief Declares the SR_LOG macros, SushiRuntime's front over SushiLog.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_category.hpp>
#include <SushiLog/log/log_front.hpp>
#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/log/log_gate.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>

/**
 * @brief Logs one record at @p level in @p category when the floor and the category's level
 *        both admit it.
 *
 * SushiLog's SL_LOG_FRONT_AT with this translation unit's K_LOG_LEVEL_FLOOR.
 */
#define SR_LOG_AT(level, category, ...)                                                          \
    SL_LOG_FRONT_AT(SushiRuntime::K_LOG_LEVEL_FLOOR, level, category, __VA_ARGS__)

/** @brief Logs step-by-step flow in a category; each {} takes the next argument. */
#define SR_LOG_TRACE(category, ...) SR_LOG_AT(SushiLog::LogLevel::Trace, category, __VA_ARGS__)

/** @brief Logs internal state in a category; each {} takes the next argument. */
#define SR_LOG_DEBUG(category, ...) SR_LOG_AT(SushiLog::LogLevel::Debug, category, __VA_ARGS__)

/** @brief Logs a lifecycle event in a category; each {} takes the next argument. */
#define SR_LOG_INFO(category, ...) SR_LOG_AT(SushiLog::LogLevel::Info, category, __VA_ARGS__)

/** @brief Logs a recovery or a fallback in a category; each {} takes the next argument. */
#define SR_LOG_WARN(category, ...) SR_LOG_AT(SushiLog::LogLevel::Warn, category, __VA_ARGS__)

/** @brief Logs a failed operation in a category; each {} takes the next argument. */
#define SR_LOG_ERROR(category, ...) SR_LOG_AT(SushiLog::LogLevel::Error, category, __VA_ARGS__)

/**
 * @brief Logs why the process cannot continue, after every queued record, and flushes; the
 *        caller then ends the process.
 */
#define SR_LOG_FATAL(category, ...) SR_LOG_AT(SushiLog::LogLevel::Fatal, category, __VA_ARGS__)
