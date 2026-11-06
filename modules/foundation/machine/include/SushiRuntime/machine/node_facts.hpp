/**************************************************************************/
/* node_facts.hpp                                                         */
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
 * @file node_facts.hpp
 * @brief Declares NodeFacts, what one machine reports about itself.
 * @author Mustafa Garip
 */

#pragma once

#include <string>
#include <vector>

#include <SushiRuntime/machine/device_facts.hpp>
#include <SushiRuntime/machine/node_label.hpp>
#include <SushiRuntime/machine/operating_system.hpp>

namespace SushiRuntime
{
    /**
     * @brief Describes one machine: its name, its operating system, its labels and its devices.
     */
    struct NodeFacts
    {
        std::string name; /**< Name of the machine. */
        OperatingSystem operating_system = OperatingSystem::Unknown; /**< System it runs. */
        std::vector<NodeLabel> labels;    /**< Labels, in the order given. */
        std::vector<DeviceFacts> devices; /**< One entry per device. */
    };
} // namespace SushiRuntime
