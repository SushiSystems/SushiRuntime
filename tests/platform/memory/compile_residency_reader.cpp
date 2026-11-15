/**************************************************************************/
/* compile_residency_reader.cpp                                           */
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
 * @file compile_residency_reader.cpp
 * @brief Compiles residency_reader.hpp on its own and writes a consumer against the reader port alone.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/residency_reader.hpp>

namespace
{
    using SushiRuntime::Internal::Memory::ResidencyReader;

    /** @brief Reads the directory through ResidencyReader, naming no writer header and no concrete class. */
    [[maybe_unused]] std::size_t consume(const ResidencyReader& reader, SushiRuntime::GlobalHandle handle)
    {
        static_cast<void>(reader.resolve(handle, SushiRuntime::MemorySpaceID(0)));
        static_cast<void>(reader.instance_kind(handle, SushiRuntime::MemorySpaceID(0)));
        static_cast<void>(reader.is_stale(handle));
        return reader.inspect(handle).size + reader.home_of(handle).layout.size;
    }
} // namespace
