/**************************************************************************/
/* numa_reader.hpp                                                        */
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
 * @file numa_reader.hpp
 * @brief Declares NUMAReader, the port HardwareDiscovery reads the NUMA layout through.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <optional>
#include <string>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Answers where the host CPU sits and which PCI address a GPU has.
         * @see modules/platform/topology/README.md
         */
        class NUMAReader
        {
            public:
                virtual ~NUMAReader() = default;

                /** @brief Returns whether the reader holds a topology at all. */
                [[nodiscard]] virtual bool loaded() const noexcept = 0;

                /** @brief Returns the NUMA node of the first processing unit, or nothing. */
                [[nodiscard]] virtual std::optional<int> cpu_numa_node() const = 0;

                /**
                 * @brief Returns the PCI address text of the first GPU the topology lists whose
                 *        name @p device_name holds, or an empty text when none matches.
                 * @param device_name The name the device runtime reports.
                 */
                [[nodiscard]] virtual std::string gpu_pci_address(const std::string& device_name) const = 0;

            protected:
                NUMAReader() = default;
                NUMAReader(const NUMAReader&) = default;
                NUMAReader& operator=(const NUMAReader&) = default;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
