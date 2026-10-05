/****************************************************************/
/* fluent.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file fluent.hpp
 * @brief Includes every header of the fluent v1 API.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/README.md
 */

#pragma once

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
    // Graph stays API::Graph because Graph already names a namespace at this scope.
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
