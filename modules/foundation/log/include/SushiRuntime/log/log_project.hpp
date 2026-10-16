/**************************************************************************/
/* log_project.hpp                                                        */
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
 * @file log_project.hpp
 * @brief Names SushiLog's LogProject and its level calls in SushiRuntime.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_project.hpp>

#include <SushiRuntime/log/log_level.hpp>

namespace SushiRuntime
{
    /** @brief A project's name and the variable that sets its levels; SushiLog owns it. */
    using LogProject = SushiLog::LogProject;

    using SushiLog::log_level;
    using SushiLog::set_log_level;
} // namespace SushiRuntime
