/**************************************************************************/
/* bound_operands.cpp                                                      */
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
 * @file bound_operands.cpp
 * @brief Defines bound_operands() over the engine's kernel pointer scope.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#include <SushiRuntime/api/graph/bound_operands.hpp>

#include <SushiRuntime/task/kernel_pointers.hpp>

namespace SushiRuntime
{
    BoundOperands bound_operands() noexcept
    {
        const Internal::Graph::KernelPointers* open = Internal::Graph::KernelPointerScope::current();
        if (open == nullptr)
            return BoundOperands();
        return BoundOperands(open->table(), open->size());
    }
} // namespace SushiRuntime