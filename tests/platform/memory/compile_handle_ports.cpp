/**************************************************************************/
/* compile_handle_ports.cpp                                               */
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
 * @file compile_handle_ports.cpp
 * @brief Compiles handle_ports.hpp on its own and writes a consumer against the ports alone.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/handle_ports.hpp>

namespace
{
    using SushiRuntime::Internal::Memory::ElementLayout;
    using SushiRuntime::Internal::Memory::HandlePorts;

    /** @brief Takes every role of the handle table through HandlePorts, naming no concrete class and no execution header. */
    [[maybe_unused]] std::size_t consume(const HandlePorts& ports, SushiRuntime::GlobalHandle handle)
    {
        const SushiRuntime::MintOutcome minted =
            ports.registry.mint(SushiRuntime::HandleKind::Buffer, SushiRuntime::HandleFlags::None);
        static_cast<void>(ports.registry.describe(minted.handle, 8, ElementLayout{}, SushiRuntime::MemorySpaceID(0)));
        static_cast<void>(ports.residency_reader.inspect(handle));
        static_cast<void>(ports.residency_writer.drop_instance(handle, SushiRuntime::MemorySpaceID(0)));
        static_cast<void>(ports.host_writes.record_host_write(handle));
        static_cast<void>(ports.history.retire_coherence_history());
        ports.retirement.set_retire_observer(nullptr);
        static_cast<void>(ports.registry.retire(minted.handle));
        return ports.registry.live_count();
    }
} // namespace
