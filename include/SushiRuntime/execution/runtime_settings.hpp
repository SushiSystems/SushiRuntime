/**************************************************************************/
/* runtime_settings.hpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Execution
    {
        /**
         * @brief Whether workers are bound to specific cores or left to the OS.
         *
         * Pinning is what lets a host application reserve cores: bound workers
         * cannot drift onto the core the render or audio thread is using. Float
         * gives that up in exchange for letting the OS scheduler move a worker
         * off a core that a higher-priority thread has taken over, which is the
         * better trade on an oversubscribed machine or inside a container whose
         * CPU allowance is enforced by quota rather than by affinity.
         */
        enum class PinPolicy
        {
            Pin,   /**< Bind worker i to core (first_core + i). */
            Float  /**< Leave placement to the OS scheduler. */
        };

        /**
         * @brief Runtime-wide settings chosen once, when the runtime is created.
         *
         * Separate from the hardware filter: this shapes the runtime itself
         * (threads, placement, background work) rather than which devices are
         * selected. Every field defaults to the behaviour a deterministic
         * simulation wants, because that is the primary workload — a batch
         * consumer opts out of the parts it does not need.
         *
         * The type lives in the Execution layer rather than in API because
         * RuntimeContext and TaskScheduler consume it, and a lower layer may not
         * depend on a higher one. API::RuntimeConfig is the public spelling of
         * this same type.
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
             * @brief How many worker threads to run. 0 means one per logical core.
             *
             * A runtime inside a game is not the machine's only tenant: it shares
             * it with a main thread, a render thread, an audio callback, asset
             * I/O and the compositor. Taking every core means the host's own
             * threads are preempted by workers. Naming a smaller count, together
             * with first_core, is how the host reserves cores for itself.
             *
             * Clamped to Core::MAX_WORKER_THREADS.
             */
            std::size_t worker_count = 0;

            /**
             * @brief First core of the range workers are pinned into.
             *
             * Worker i binds to core (first_core + i), so a host that keeps its
             * own threads on cores 0..3 sets first_core = 4. Ignored under
             * PinPolicy::Float. If the range would run past the machine's last
             * core the runtime clamps it and logs what it did, rather than
             * pinning to core indices the OS will silently ignore.
             */
            std::size_t first_core = 0;

            /** @brief Whether workers are bound to cores at all. */
            PinPolicy pinning = PinPolicy::Pin;

            /**
             * @brief Run the background thermal rebalancer.
             *
             * Off by default. It migrates tasks between workers on a 5 ms
             * heartbeat, which trades run-to-run reproducibility and a low jitter
             * floor for throughput on long batch work — the opposite of what a
             * simulation frame wants, and the runtime's primary consumer is a
             * simulation. Throughput workloads turn it on explicitly.
             */
            bool rebalancer = false;
        };

        /**
         * @brief The worker pool as actually built, which may differ from the request.
         *
         * RuntimeSettings is what the caller asked for; this is what the runtime
         * could give. They diverge when the requested core range does not fit the
         * machine, or when the count exceeds Core::MAX_WORKER_THREADS. A host that
         * reserved cores should verify against this rather than against its own
         * request — the whole point of reserving is that it actually happened.
         */
        struct WorkerPoolInfo
        {
            std::size_t count      = 0;   /**< Worker threads running. */
            std::size_t first_core = 0;   /**< First bound core; meaningless when floating. */
            PinPolicy   pinning    = PinPolicy::Pin;  /**< Whether workers are bound at all. */
        };

    } // namespace Execution
} // namespace SushiRuntime
