/**************************************************************************/
/* system_memory_probe.hpp                                                */
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
 * @file system_memory_probe.hpp
 * @brief Declares SystemMemoryProbe, the MemoryProbe that asks the operating system for host memory.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/memory_probe.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Reads the physical and available memory of the machine from the operating system.
         *
         * A query the system refuses reports zero bytes.
         */
        class SUSHIRUNTIME_API SystemMemoryProbe final : public MemoryProbe
        {
            public:
                [[nodiscard]] ProbedMemory probe() const override;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
