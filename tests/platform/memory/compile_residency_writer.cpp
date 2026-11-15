/**************************************************************************/
/* compile_residency_writer.cpp                                           */
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
 * @file compile_residency_writer.cpp
 * @brief Compiles residency_writer.hpp on its own and writes a consumer against the writer port alone.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/residency_writer.hpp>

namespace
{
    using SushiRuntime::Internal::Memory::ResidencyWriter;

    /** @brief Changes the directory through ResidencyWriter, naming no reader header and no concrete class. */
    [[maybe_unused]] bool consume(ResidencyWriter& writer, SushiRuntime::GlobalHandle handle,
                                  void* address)
    {
        const SushiRuntime::MemorySpaceID space(1);
        const SushiRuntime::HandleStatus set = writer.set_instance(handle, space, address);
        const SushiRuntime::HandleStatus dropped = writer.drop_instance(handle, space);
        return writer.coherence(handle) && set == dropped;
    }
} // namespace
