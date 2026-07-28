/**************************************************************************/
/* late_bindings.hpp                                                      */
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

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    namespace Graph
    {
        /**
         * @brief Per-step execution controls re-read at dispatch, not baked at compile.
         *
         * The compiled DAG fixes a node's topology and which resources it touches,
         * but a per-frame engine must vary two things every step without paying a
         * recompile: how many elements the kernel iterates (entities spawned or
         * destroyed, a changing count of active cells) and whether the node runs at
         * all (a sleeping island, an LOD-disabled system). Both live here, behind a
         * stable shared_ptr the fluent layer re-points between steps and the
         * scheduler reads when it dispatches the node. A node with no bindings keeps
         * the old compile-time behaviour at the cost of a single null-pointer load.
         *
         * The fields are atomic only to be free of torn reads: the writes happen on
         * the driver thread at a step boundary (after the previous step's completion
         * barrier) and the reads happen on a worker after the task is handed off
         * through the scheduler's acquire/release submit path, so the
         * happens-before is supplied by that machinery and relaxed ordering suffices.
         */
        struct LateBindings
        {
            /** @brief count sentinel: iterate the capacity baked at compile time. */
            static constexpr std::size_t USE_BAKED = ~std::size_t{0};

            /** @brief When false the node is instant-complete: no kernel, successors fire at once. */
            std::atomic<bool> enabled{true};

            /** @brief Live iteration size for this step, or USE_BAKED for the full baked range. */
            std::atomic<std::size_t> count{USE_BAKED};

            /**
             * @brief Host-readable location a predecessor writes the live count to.
             *
             * The count above answers "how many elements does the host already know
             * we want". This answers the harder question a producer/consumer chain
             * asks: how many did the *device* just produce. A broadphase writes its
             * pair count, and the narrowphase that consumes it must iterate exactly
             * that many — a number that does not exist yet when the step boundary
             * polls the host-side providers.
             *
             * It works because a node's command group is built on a worker at
             * dispatch, and a node is only dispatched once every predecessor's event
             * has completed and been waited on, so a value a predecessor kernel wrote
             * is visible here. Null on every ordinary node, in which case the
             * atomic count above is used.
             *
             * Written once when the task is recorded — before any run — and only read
             * afterwards, so unlike the two fields above it needs no atomicity. The
             * allocation must be host-addressable (shared USM); the API that sets it
             * rejects a device-resident buffer at the call site.
             */
            const std::uint32_t* device_count = nullptr;

            /** @brief Element index of the live count within device_count. */
            std::size_t device_count_index = 0;
        };

    } // namespace Graph
} // namespace SushiRuntime
