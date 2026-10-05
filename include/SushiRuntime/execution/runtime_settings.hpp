/****************************************************************/
/* runtime_settings.hpp                                         */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file runtime_settings.hpp
 * @brief Declares PinPolicy, RuntimeSettings and WorkerPoolInfo.
 * @author Mustafa Garip
 * @see include/SushiRuntime/execution/README.md
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Execution
    {
        /**
         * @brief Selects whether workers are bound to specific cores or left to the OS.
         * @see include/SushiRuntime/execution/README.md
         */
        enum class PinPolicy
        {
            Pin,   /**< Bind worker i to core (first_core + i). */
            Float  /**< Leave placement to the OS scheduler. */
        };

        /**
         * @brief Holds the runtime-wide settings chosen once, when the runtime is created.
         * @see include/SushiRuntime/execution/README.md
         */
        struct RuntimeSettings
        {
            /**
             * @brief Build queues with profiling so RunReport carries device timing.
             *
             * Construction-time because SYCL queues are created once and not
             * rebuilt to toggle profiling. Off by default: the queues and the
             * dispatch hot path are then exactly as without this struct, reading
             * no timestamps.
             */
            bool profiling = false;

            /**
             * @brief Sets how many worker threads run; 0 means one per logical core.
             * @see include/SushiRuntime/execution/README.md
             */
            std::size_t worker_count = 0;

            /**
             * @brief Sets the first core of the range workers are pinned into.
             * @see include/SushiRuntime/execution/README.md
             */
            std::size_t first_core = 0;

            /** @brief Whether workers are bound to cores at all. */
            PinPolicy pinning = PinPolicy::Pin;

            /**
             * @brief Turns on the background thermal rebalancer; off by default.
             * @see include/SushiRuntime/execution/README.md
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

        /**
         * @brief Reports the worker pool as built, which may differ from the request.
         * @see include/SushiRuntime/execution/README.md
         */
        struct WorkerPoolInfo
        {
            std::size_t count      = 0;   /**< Worker threads running. */
            std::size_t first_core = 0;   /**< First bound core; meaningless when floating. */
            PinPolicy   pinning    = PinPolicy::Pin;  /**< Whether workers are bound at all. */
        };

    } // namespace Execution
} // namespace SushiRuntime
