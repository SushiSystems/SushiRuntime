/**************************************************************************/
/* in_memory_numa_reader.hpp                                              */
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
 * @file in_memory_numa_reader.hpp
 * @brief Declares InMemoryNUMAReader, a NUMAReader that answers what it was built with.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/topology/numa_reader.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /** @brief One GPU an InMemoryNUMAReader lists: its name and its PCI address text. */
        struct ListedGPU
        {
            std::string name;    /**< The name a device's name must hold to match. */
            std::string address; /**< The PCI address text it answers. */
        };

        /**
         * @brief Answers a fixed CPU node and a fixed list of GPUs, matched as hwloc's are.
         * @see modules/platform/topology/README.md
         */
        class InMemoryNUMAReader final : public NUMAReader
        {
            public:
                /**
                 * @param cpu_node The node cpu_numa_node() answers.
                 * @param gpus The GPUs, in the order gpu_pci_address() tries them.
                 * @param is_loaded What loaded() answers.
                 */
                explicit InMemoryNUMAReader(std::optional<int> cpu_node = std::nullopt,
                                            std::vector<ListedGPU> gpus = {}, bool is_loaded = true)
                    : cpu_node_(cpu_node), gpus_(std::move(gpus)), loaded_(is_loaded)
                {
                }

                [[nodiscard]] bool loaded() const noexcept override { return loaded_; }

                [[nodiscard]] std::optional<int> cpu_numa_node() const override { return cpu_node_; }

                [[nodiscard]] std::string gpu_pci_address(const std::string& device_name) const override
                {
                    for (const ListedGPU& gpu : gpus_)
                        if (device_name.find(gpu.name) != std::string::npos)
                            return gpu.address;
                    return {};
                }

            private:
                /// @brief The node cpu_numa_node() answers.
                std::optional<int> cpu_node_;
                /// @brief The GPUs, in the order they are tried.
                std::vector<ListedGPU> gpus_;
                /// @brief What loaded() answers.
                bool loaded_;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
