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
