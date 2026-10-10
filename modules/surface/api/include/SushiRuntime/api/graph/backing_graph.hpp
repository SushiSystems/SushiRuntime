/**************************************************************************/
/* backing_graph.hpp                                                      */
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
 * @file backing_graph.hpp
 * @brief Names the lower-level graph builder the fluent layer records onto.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <type_traits>

#include <SushiRuntime/graph/task_graph.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /** @brief Names the lower-level graph builder this layer wraps. */
        using BackingGraph = ::SushiRuntime::Graph::TaskGraph;

        /** @brief A graph emitter that records nodes on a backing graph: void(BackingGraph&). */
        template <class F>
        inline constexpr bool is_graph_emitter_v = std::is_invocable_v<F, BackingGraph&>;

    } // namespace API
} // namespace SushiRuntime
