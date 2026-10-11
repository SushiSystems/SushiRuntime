/**************************************************************************/
/* chain_plan.hpp                                                         */
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
 * @file chain_plan.hpp
 * @brief Declares classify_chain and aliases the chain plan types it fills.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/graph/chain_plan.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;

        /** @brief Names Internal::Graph::PendingEvent under its former namespace. */
        using PendingEvent = Internal::Graph::PendingEvent;

        /** @brief Names Internal::Graph::ChainPlan under its former namespace. */
        using ChainPlan = Internal::Graph::ChainPlan;

        /**
         * @brief Classifies a compiled plan for the in-order chain.
         * @param graph A plan after capture_base_state().
         * @param context The context whose USM registry owns the plan's regions.
         * @return The classification; not chainable on any doubt.
         * @see modules/engine/execution/README.md
         */
        SUSHIRUNTIME_API ChainPlan classify_chain(const Internal::Graph::CompiledGraph& graph,
                                                  RuntimeContext& context);

    } // namespace Execution
} // namespace SushiRuntime
