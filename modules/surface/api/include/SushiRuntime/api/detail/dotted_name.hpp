/**************************************************************************/
/* dotted_name.hpp                                                        */
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
 * @file dotted_name.hpp
 * @brief Declares the check a placement class name is held to.
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <string_view>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns whether a class name is non-empty and has no empty dotted segment.
         * @param name The name to test, such as "sushiai.replica".
         */
        [[nodiscard]] constexpr bool is_dotted_name(std::string_view name) noexcept
        {
            if (name.empty() || name.front() == '.' || name.back() == '.')
                return false;
            return name.find("..") == std::string_view::npos;
        }
    } // namespace Detail
} // namespace SushiRuntime
