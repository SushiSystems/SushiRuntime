/**************************************************************************/
/* bound_operands.hpp                                                      */
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
 * @file bound_operands.hpp
 * @brief Declares bound_operands(), the view of the operands a task body bound by handle.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/bound_operands.hpp>

namespace SushiRuntime
{
    /**
     * @brief Returns the operands the task being dispatched on this thread bound on its device.
     *
     * The view is the handle module's BoundOperands, read by position with the element type checked.
     * @return An empty view outside such a body.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API BoundOperands bound_operands() noexcept;
} // namespace SushiRuntime
