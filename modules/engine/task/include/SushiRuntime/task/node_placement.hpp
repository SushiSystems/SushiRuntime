/**************************************************************************/
/* node_placement.hpp                                                     */
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
 * @file node_placement.hpp
 * @brief Declares placement_of, which names the device a graph node runs on.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        class USMAllocator;
    }

    namespace Internal::Graph
    {
        struct Node;

        /** @brief Device a node runs on when it names no owned region and states none. */
        inline constexpr std::size_t K_DEFAULT_PLACEMENT = 0;

        /**
         * @brief Returns the device @p node runs on.
         * @param node A node whose regions and stated placement are final.
         * @param memory The allocator that knows which device owns each region and its context.
         * @return The device by the rule order in the README, or an InvalidGraph error when
         *         the stated device and the memory's owner are in different contexts.
         * @see modules/engine/task/README.md
         */
        SUSHIRUNTIME_API Result<std::size_t> placement_of(const Node& node,
                                                          const Internal::Memory::USMAllocator& memory);

    } // namespace Graph
} // namespace SushiRuntime
