/**************************************************************************/
/* environment.cpp                                                        */
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
 * @file environment.cpp
 * @brief Implements read_environment() over the C runtime's bounds-checked read.
 * @author Mustafa Garip
 */

#include <SushiRuntime/process_environment/environment.hpp>

#include <cstdlib>

namespace SushiRuntime
{
    namespace Distributed
    {
#if defined(_MSC_VER)
        std::optional<std::string> read_environment(const char* name)
        {
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
                return std::nullopt;

            // _dupenv_s hands over a copy this function owns and must free.
            std::string text(value);
            std::free(value);
            return text;
        }
#else
        std::optional<std::string> read_environment(const char* name)
        {
            const char* value = std::getenv(name);
            if (value == nullptr)
                return std::nullopt;
            return std::string(value);
        }
#endif

    } // namespace Distributed
} // namespace SushiRuntime
