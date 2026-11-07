/**************************************************************************/
/* hwloc_numa_reader.hpp                                                  */
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
 * @file hwloc_numa_reader.hpp
 * @brief Declares HwlocNUMAReader, the NUMAReader over the machine's hwloc topology.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/numa_reader.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Loads the machine's hwloc topology, I/O devices included, and reads it.
         *
         * A topology hwloc refuses to load leaves the reader empty: loaded() is false and
         * every answer is nothing.
         * @see modules/platform/topology/README.md
         */
        class SUSHIRUNTIME_API HwlocNUMAReader final : public NUMAReader
        {
            public:
                /** @brief Loads the topology. */
                HwlocNUMAReader();
                ~HwlocNUMAReader() override;

                HwlocNUMAReader(const HwlocNUMAReader&) = delete;
                HwlocNUMAReader& operator=(const HwlocNUMAReader&) = delete;

                [[nodiscard]] bool loaded() const noexcept override;
                [[nodiscard]] std::optional<int> cpu_numa_node() const override;
                [[nodiscard]] std::string gpu_pci_address(const std::string& device_name) const override;

            private:
                /** @brief Holds the loaded hwloc topology. */
                struct Loaded;

                /// @brief The loaded topology; never null, its handle null when hwloc refused.
                std::unique_ptr<Loaded> loaded_;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
