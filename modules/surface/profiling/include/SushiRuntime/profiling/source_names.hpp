/**************************************************************************/
/* source_names.hpp                                                       */
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
 * @file source_names.hpp
 * @brief Declares K_DEVICE_SOURCE_NAME, the name the runtime's device source is registered under.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <string_view>

namespace SushiRuntime
{
    /** @brief Names the SushiProfiler source that reports the runtime's device work, in a configuration and in a record. */
    inline constexpr std::string_view K_DEVICE_SOURCE_NAME = "sushiruntime.device";
} // namespace SushiRuntime
