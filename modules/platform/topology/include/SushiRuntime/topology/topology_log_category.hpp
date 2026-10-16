/**************************************************************************/
/* topology_log_category.hpp                                              */
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
 * @file topology_log_category.hpp
 * @brief Declares K_TOPOLOGY_LOG_CATEGORY, the log category of the topology module.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_category.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>

namespace SushiRuntime
{
    /** @brief Names the records the topology module logs. */
    inline constexpr SushiLog::LogCategory K_TOPOLOGY_LOG_CATEGORY =
        SushiLog::make_log_category(K_SUSHIRUNTIME_LOG_PROJECT, "topology");
} // namespace SushiRuntime
