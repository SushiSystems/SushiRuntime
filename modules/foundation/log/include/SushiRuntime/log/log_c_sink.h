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
 * @brief Declares sr_set_log_sink and sr_set_log_synchronous, SushiLog's C sink.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiLog/log/log_c_sink.h>

/**
 * @brief Replaces the sink of the process with @p function, called with @p user.
 * @param function A null function restores the default, which writes to stderr.
 */
static inline void sr_set_log_sink(SushiLogSinkFunction function, void* user)
{
    sl_set_log_sink(function, user);
}

/**
 * @brief Chooses whether a record reaches the sink before its log call returns.
 * @param synchronous Nonzero waits for the queue, then delivers on the calling thread.
 */
static inline void sr_set_log_synchronous(int synchronous)
{
    sl_set_log_synchronous(synchronous);
}
