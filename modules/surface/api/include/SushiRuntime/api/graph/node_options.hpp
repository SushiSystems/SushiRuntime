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
#include <optional>
#include <string_view>

#include <SushiRuntime/core/determinism.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/placement_class.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds what a caller may state about a node beyond its launch and its access.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    struct NodeOptions
    {
        /** @brief How many parameters one node carries. */
        static constexpr std::size_t K_MAX_PARAMETERS = 8;

        /** @brief Launch width stamped into the node's event records. */
        std::size_t width = K_WIDTH_UNKNOWN;

        /** @brief Label the node reports under; copied when the node is recorded. */
        std::string_view name = {};

        /** @brief Operation the node performs; a default id names none. */
        OperationID operation = {};

        /**
         * @brief Scalars carried with the node, at most K_MAX_PARAMETERS; copied when the
         *        node is recorded.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        span<const std::uint64_t> parameters = {};

        /** @brief Group the node belongs to, for placement; a default class names none. */
        PlacementClass placement_class = {};

        /** @brief Index within the group, such as a replica or an island; 0 is a key too. */
        std::optional<std::uint64_t> placement_key = {};

        /** @brief Determinism contract the result carries; a default states none. */
        Determinism determinism = Determinism::Unstated;
    };
} // namespace SushiRuntime
