/**************************************************************************/
/* sushiruntime_log_project.hpp                                           */
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
 * @file sushiruntime_log_project.hpp
 * @brief Declares K_SUSHIRUNTIME_LOG_PROJECT, the log project of SushiRuntime.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <SushiRuntime/log/log_project.hpp>

namespace SushiRuntime
{
    /** @brief Names SushiRuntime's records, whose levels SR_LOG_LEVEL sets. */
    inline constexpr LogProject K_SUSHIRUNTIME_LOG_PROJECT{"sushiruntime", "SR_LOG_LEVEL"};
} // namespace SushiRuntime
