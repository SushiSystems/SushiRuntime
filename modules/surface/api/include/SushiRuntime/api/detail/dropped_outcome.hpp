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
