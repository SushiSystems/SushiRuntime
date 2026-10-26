/**************************************************************************/
/* profile_gate.hpp                                                       */
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
 * @file profile_gate.hpp
 * @brief Declares the compiled profiling level of a translation unit.
 * @author Mustafa Garip
 * @see modules/foundation/profile/README.md
 */

#pragma once

#include <SushiRuntime/profile/profile_config.hpp>

namespace SushiRuntime
{
    namespace Internal::Profile
    {
        /**
         * @brief Holds the deepest level this translation unit compiles in.
         *
         * Internal linkage, so two translation units compiled at different levels do not meet.
         */
        [[maybe_unused]] constexpr int K_COMPILED_LEVEL = SR_ENABLE_PROFILER != 0 ? SR_PROFILE_LEVEL : 0;
    } // namespace Internal::Profile
} // namespace SushiRuntime
