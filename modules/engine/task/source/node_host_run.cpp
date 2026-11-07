/**************************************************************************/
/* node_host_run.cpp                                                      */
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
 * @file node_host_run.cpp
 * @brief Defines Node::run_on_host(), which runs a node with no device or refuses it.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /** @brief Returns the refusal of a node that has no host form. */
            [[nodiscard]] Error no_host_form() noexcept
            {
                return Error{ErrorCode::Unsupported,
                             "the node has no host form: a host runtime runs host nodes and launches "
                             "over a count or an Extent; run an NdRange, a raw command group or a "
                             "queue body on a runtime that has a device"};
            }
        } // namespace

        Result<void> Node::run_on_host()
        {
            const bool native = mode == ExecutionMode::Host && !submits_device_work_;
            if (native)
                return Detail::translate_exceptions([this]() { execute_native(); });
            if (host_form)
                return Detail::translate_exceptions([this]() { host_form(); });
            return make_error(no_host_form());
        }
    } // namespace Internal::Graph
} // namespace SushiRuntime
