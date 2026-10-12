/**************************************************************************/
/* plan_access.hpp                                                        */
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
 * @file plan_access.hpp
 * @brief Defines the conversion between Detail::Plan and the engine's compiled graph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <SushiRuntime/api/detail/plan.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Returns the compiled graph a surface plan names; null for null. */
        [[nodiscard]] inline Internal::Graph::CompiledGraph* engine_plan(Plan* plan) noexcept
        {
            return reinterpret_cast<Internal::Graph::CompiledGraph*>(plan);
        }

        /** @brief Returns the compiled graph a surface plan names, read-only; null for null. */
        [[nodiscard]] inline const Internal::Graph::CompiledGraph*
        engine_plan(const Plan* plan) noexcept
        {
            return reinterpret_cast<const Internal::Graph::CompiledGraph*>(plan);
        }

        /** @brief Returns the surface's name for a compiled graph; null for null. */
        [[nodiscard]] inline Plan* surface_plan(Internal::Graph::CompiledGraph* graph) noexcept
        {
            return reinterpret_cast<Plan*>(graph);
        }

        /** @brief Returns the surface's name for a compiled graph, read-only; null for null. */
        [[nodiscard]] inline const Plan*
        surface_plan(const Internal::Graph::CompiledGraph* graph) noexcept
        {
            return reinterpret_cast<const Plan*>(graph);
        }
    } // namespace Detail
} // namespace SushiRuntime
