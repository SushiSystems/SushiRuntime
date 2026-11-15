/**************************************************************************/
/* compile_handle_home_reader.cpp                                         */
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
 * @file compile_handle_home_reader.cpp
 * @brief Compiles handle_home_reader.hpp on its own and writes a consumer against the port alone.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/handle/handle_home_reader.hpp>

namespace
{
    /** @brief Reads where a buffer lives through HandleHomeReader, naming no memory module header. */
    [[maybe_unused]] bool consume(const SushiRuntime::HandleHomeReader& reader, SushiRuntime::GlobalHandle handle)
    {
        const SushiRuntime::HandleHome home = reader.home_of(handle);
        const SushiRuntime::ResolveOutcome outcome = reader.resolve(handle, home.home);
        return home.resident && home.memory != SushiRuntime::HomeMemory::Unknown &&
               outcome.status == SushiRuntime::HandleStatus::Ok && home.layout.is_bytes();
    }
} // namespace
