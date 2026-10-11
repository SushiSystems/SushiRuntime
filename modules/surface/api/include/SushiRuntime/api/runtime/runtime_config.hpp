/**************************************************************************/
/* runtime_config.hpp                                                     */
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
 * @file runtime_config.hpp
 * @brief Declares RuntimeConfig, the one struct Runtime::create() takes, and its type names.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/scheduler/pin_policy.hpp>
#include <SushiRuntime/scheduler/worker_pool.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    /** @brief Names the device classes and features a runtime accepts. */
    using DeviceFilter = Internal::Topology::HardwareFilterConfig;

    /** @brief Names whether workers bind to cores. */
    using PinPolicy = Internal::Scheduler::PinPolicy;

    /** @brief Names the worker pool as built: count, first core and pin policy. */
    using WorkerPool = Internal::Scheduler::WorkerPool;

    /**
     * @brief Holds every setting chosen once, when Runtime::create() builds a runtime.
     * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
     */
    struct RuntimeConfig
    {
        DeviceFilter devices;               /**< Which devices the runtime drives. */
        std::size_t worker_count = 0;       /**< Worker threads; 0 is one per logical core. */
        std::size_t first_core = 0;         /**< First core of the range workers pin into. */
        PinPolicy pinning = PinPolicy::Pin; /**< Whether workers bind to cores at all. */
        bool profiling = false;             /**< Builds queues that record device timing. */
        bool rebalancer = false;            /**< Starts the background thermal rebalancer. */
        bool force_scheduler = false;       /**< Runs every graph on the work-stealing path. */
    };



} // namespace SushiRuntime
