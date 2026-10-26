/**************************************************************************/
/* dropped_outcome.hpp                                                    */
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
 * @file dropped_outcome.hpp
 * @brief Declares log_dropped_outcome, which records the failure of a step nobody waited for.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Logs at WARN that the outcome of an unwaited step is dropped, with the error it holds.
         * @param error What the step failed with.
         * @param where Names the call that settled the step; a string literal.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        SUSHIRUNTIME_API void log_dropped_outcome(const Error& error, const char* where);
    } // namespace Detail
} // namespace SushiRuntime
