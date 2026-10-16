/**************************************************************************/
/* execution_log_category.hpp                                             */
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
 * @file execution_log_category.hpp
 * @brief Declares K_EXECUTION_LOG_CATEGORY, the log category of the execution module.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <SushiRuntime/log/log_category.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>

namespace SushiRuntime
{
    /** @brief Names the records the execution module logs. */
    inline constexpr LogCategory K_EXECUTION_LOG_CATEGORY =
        make_log_category(K_SUSHIRUNTIME_LOG_PROJECT, "execution");
} // namespace SushiRuntime
