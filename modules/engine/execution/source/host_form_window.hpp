/**************************************************************************/
/* host_form_window.hpp                                                   */
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
 * @file host_form_window.hpp
 * @brief Declares run_inside_window(), which runs a node on the host with the device gate open for its host form.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <optional>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/memory/device_access_gate.hpp>
#include <SushiRuntime/memory/device_access_scope.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Runs a node on the host; a kernel's host form runs with the gate open, native host work with it shut.
         * @param gate Null runs the node with no window.
         * @see modules/engine/execution/README.md
         */
        [[nodiscard]] inline Result<void> run_inside_window(Internal::Graph::Node& node,
                                                            Internal::Memory::DeviceAccessGate* gate)
        {
            const bool native = node.mode == Internal::Scheduler::Task::ExecutionMode::Host &&
                                !node.submits_device_work();
            std::optional<Internal::Memory::DeviceAccessScope> window;
            if (!native)
                window.emplace(gate);
            return node.run_on_host();
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
