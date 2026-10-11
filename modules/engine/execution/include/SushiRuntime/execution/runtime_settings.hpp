/**************************************************************************/
/* runtime_settings.hpp                                                   */
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
 * @file runtime_settings.hpp
 * @brief Declares RuntimeSettings, its worker pool mapping and the pool type names.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/scheduler/pin_policy.hpp>
#include <SushiRuntime/scheduler/worker_pool_info.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        /** @brief Names Scheduler::PinPolicy in the Execution namespace. */
        using PinPolicy = Scheduler::PinPolicy;

        /** @brief Names Scheduler::WorkerPoolInfo in the Execution namespace. */
        using WorkerPoolInfo = Scheduler::WorkerPoolInfo;

        /**
         * @brief Holds the runtime-wide settings chosen once, when the runtime is created.
         * @see modules/engine/execution/README.md
         */
        struct RuntimeSettings
        {
            /**
             * @brief Build queues with profiling so RunReport carries device timing.
             *
             * Fixed at construction, because the SYCL queues are built once.
             * Off by default; with it off, no timestamps are read.
             */
            bool profiling = false;

            /**
             * @brief Sets how many worker threads run; 0 means one per logical core.
             * @see modules/engine/execution/README.md
             */
            std::size_t worker_count = 0;

            /**
             * @brief Sets the first core of the range workers are pinned into.
             * @see modules/engine/execution/README.md
             */
            std::size_t first_core = 0;

            /** @brief Whether workers are bound to cores at all. */
            PinPolicy pinning = PinPolicy::Pin;

            /**
             * @brief Turns on the background thermal rebalancer; off by default.
             * @see modules/engine/execution/README.md
             */
            bool rebalancer = false;

            /**
             * @brief Runs every graph on the work-stealing scheduler, never on the in-order chain.
             *
             * Off by default: a chainable plan then submits from the calling
             * thread to one in-order queue. On, every run takes the scheduler
             * path, for comparison and debugging. RunReport::layer names the
             * path a run took either way.
             */
            bool force_scheduler = false;
        };

        /** @brief Returns the worker pool a RuntimeSettings asks the scheduler for. */
        [[nodiscard]] inline Scheduler::WorkerPoolSettings
        worker_pool_settings(const RuntimeSettings& settings) noexcept
        {
            Scheduler::WorkerPoolSettings pool;
            pool.worker_count = settings.worker_count;
            pool.first_core = settings.first_core;
            pool.pinning = settings.pinning;
            return pool;
        }

    } // namespace Execution
} // namespace SushiRuntime
