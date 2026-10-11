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
 * @file dispatch_placement.hpp
 * @brief Declares the placement of a graph node on a device at its first dispatch.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Graph { struct Node; }
    namespace Memory { class USMAllocator; }

    namespace Scheduler
    {
        /**
         * @brief Checks that a node's regions share one device and caches the device it runs on.
         * @param node Its cached queue index and its checked flag are written on success.
         * @param memory Asked which device owns each region of the node.
         * @param device_count Devices the runtime holds; a placement at or past it is refused.
         * @throws Core::SushiException with Errc::invalid_graph; nothing is cached then.
         * @see modules/engine/scheduler/README.md
         */
        void place_for_dispatch(Graph::Node& node, const Memory::USMAllocator& memory,
                                std::size_t device_count);

    } // namespace Scheduler
} // namespace SushiRuntime
