/**************************************************************************/
/* dispatch_placement.hpp                                                 */
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
 * @file dispatch_placement.hpp
 * @brief Declares the placement of a graph node on a device at its first dispatch.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }
    namespace Internal::Memory { class USMAllocator; }

    namespace Internal::Scheduler
    {
        /**
         * @brief Checks that a node's regions share one device and caches the device it runs on;
         *        a node already placed keeps its device and is not checked again.
         * @param node Its cached index and checked flag are written on success.
         * @param memory Asked which device owns each region of the node.
         * @param device_count Devices the runtime holds; a placement at or past it is refused.
         * @return Nothing, or an InvalidGraph error; nothing is cached then.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void>
        place_for_dispatch(Internal::Graph::Node& node, const Internal::Memory::USMAllocator& memory,
                           std::size_t device_count);

    } // namespace Scheduler
} // namespace SushiRuntime
