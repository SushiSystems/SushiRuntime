/****************************************************************/
/* fluent.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

/**
 * @file fluent.hpp
 * @brief Umbrella for the fluent v1 API — the surface most users program against.
 *
 * The fluent layer is split by concern into focused, symmetric folders; this is
 * the single public include that pulls them together:
 *   - data/        the data handles (Buffer, State)
 *   - launch/      the iteration geometry (Extent, NdRange)
 *   - vocabulary/  the data-flow DSL (Reads/Writes, In/Out/InOut, Dynamic) and
 *                  the reduction combiners (Sum, Minimum, Maximum)
 *   - graph/       the chainable builders (Graph, Region, DynamicGraph) and
 *                  RunHandle, the receipt for a step started asynchronously
 *   - runtime/     the Runtime entry point, RuntimeConfig, and the Cluster handle
 *   - detail/      callable traits, the per-step StepMachinery, report building
 */

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/captures.hpp>
#include <SushiRuntime/api/vocabulary/combine.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/api/graph/graph.hpp>
#include <SushiRuntime/api/graph/region.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/api/graph/dynamic_graph.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/api/runtime/cluster.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    // Promote the entry points into the SushiRuntime namespace so users write
    // `Runtime::create()` after `using namespace SushiRuntime;`. The graph type
    // stays API::Graph (reached through auto) because the name Graph already
    // denotes a namespace at this scope. The data and launch handles carry their
    // own promotions in their leaf headers.
    using API::Runtime;
    using API::RuntimeConfig;
    using API::RunHandle;
    using API::Reads;
    using API::Writes;
    using API::In;
    using API::Out;
    using API::InOut;
    using API::LateCount;
    using Core::Error;
    using Core::Errc;
    using Core::RunReport;
    using Core::SushiException;

} // namespace SushiRuntime
