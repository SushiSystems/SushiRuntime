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
 * @brief Declares BackingGraph, a deprecated alias of the engine's TaskGraph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <SushiRuntime/graph/task_graph.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /** @brief Names Graph::TaskGraph under a deprecated alias. */
        using BackingGraph [[deprecated("use SushiRuntime::Graph::TaskGraph")]] =
            ::SushiRuntime::Graph::TaskGraph;

    } // namespace API
} // namespace SushiRuntime
