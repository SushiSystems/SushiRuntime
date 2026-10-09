/**************************************************************************/
/* fluent.hpp                                                             */
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
 * @file fluent.hpp
 * @brief Includes every header of the fluent v1 API.
 * @author Mustafa Garip
 * @see modules/surface/api/README.md
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
