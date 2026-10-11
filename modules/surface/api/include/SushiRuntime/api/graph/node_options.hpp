/**************************************************************************/
/* node_options.hpp                                                       */
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
 * @file node_options.hpp
 * @brief Declares NodeOptions, the optional settings of one recorded node.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/observability/dispatch_width.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds what a caller may state about a node beyond its launch and its access.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    struct NodeOptions
    {
        /** @brief Launch width stamped into the node's event records. */
        std::size_t width = Internal::Observability::K_WIDTH_UNKNOWN;

        /** @brief Label the node reports under; copied when the node is recorded. */
        std::string_view name;

        /** @brief Operation the node performs; a default id names none. */
        OperationID operation;

        /**
         * @brief Scalars carried with the node; copied when the node is recorded.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        span<const std::uint64_t> parameters;
    };
} // namespace SushiRuntime
