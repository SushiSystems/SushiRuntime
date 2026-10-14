/**************************************************************************/
/* context_factory.hpp                                                    */
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
 * @file context_factory.hpp
 * @brief Declares the helper that builds the RuntimeContext a test runs on.
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Returns a context built with the arguments RuntimeContext::create() takes.
         * @pre A context can be built; the process stops, naming the error, when none can.
         */
        [[nodiscard]] inline std::shared_ptr<Internal::Execution::RuntimeContext> make_context(
            const DeviceFilter& config = {},
            std::shared_ptr<Internal::Topology::TopologyProvider> provider = nullptr,
            std::shared_ptr<Internal::Topology::NUMADistanceModel> distance_model = nullptr,
            const Internal::Execution::RuntimeSettings& settings = {})
        {
            return Internal::Execution::RuntimeContext::create(
                       config, std::move(provider), std::move(distance_model), settings)
                .value();
        }
    } // namespace Test
} // namespace SushiRuntime
