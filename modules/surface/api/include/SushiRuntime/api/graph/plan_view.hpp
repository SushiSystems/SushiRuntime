/**************************************************************************/
/* plan_view.hpp                                                          */
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
 * @file plan_view.hpp
 * @brief Declares PlanView, the read-only copy of what a compiled plan orders.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    /**
     * @brief Describes one node of a compiled plan.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    struct PlanNode
    {
        /** @brief Position of the rule that recorded the node, as rule_registration() takes it. */
        std::size_t rule = 0;

        /** @brief Label the node reports under, as a RunReport row names it. */
        std::string name;

        /** @brief Operation the node names; a default id names none. */
        OperationID operation;

        /** @brief How many nodes of the plan must finish before this one starts. */
        std::size_t predecessor_count = 0;

        /** @brief Regions the node reads, in the order it named them. */
        std::vector<BufferRegion> reads;

        /** @brief Regions the node writes, in the order it named them. */
        std::vector<BufferRegion> writes;
    };

    /**
     * @brief Holds a copy of every node of a compiled plan, in recording order.
     *
     * It owns what it reports, so it stays readable after the graph recompiles or is
     * destroyed.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    struct PlanView
    {
        /** @brief One entry per recorded node, in recording order. */
        std::vector<PlanNode> nodes;
    };
} // namespace SushiRuntime
