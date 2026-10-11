/**************************************************************************/
/* node_widths.hpp                                                        */
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
 * @file node_widths.hpp
 * @brief Declares NodeWidths and the function that reads them off a node.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/observability/dispatch_width.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct Node;

        /** @brief Pairs the range a dispatch launched over with the count that did work. */
        struct NodeWidths
        {
            std::uint32_t launched = Internal::Observability::K_WIDTH_UNKNOWN; /**< Elements the dispatch launched over. */
            std::uint32_t live     = Internal::Observability::K_WIDTH_UNKNOWN; /**< Elements that did work. */
        };

        /**
         * @brief Reads @p node's widths as they stand at submit.
         * @param node A compiled node, with or without late bindings.
         * @return The baked width for both, narrowed by the bindings' live count when it holds one.
         */
        SUSHIRUNTIME_API NodeWidths snapshot_widths(const Node& node) noexcept;

    } // namespace Graph
} // namespace SushiRuntime
