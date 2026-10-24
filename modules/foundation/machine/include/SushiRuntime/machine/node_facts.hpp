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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
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
