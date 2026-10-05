/****************************************************************/
/* late_bindings.hpp                                            */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
             * The node's kernel reads it on the device, runs at its capacity and
             * returns past the live count, so no host read happens between nodes
             * (docs/design/GRAPH_REPLAY.md §D). Null on
             * every ordinary node, in which case the atomic count above is used.
             *
             * Written once when the task is recorded — before any run — and only read
             * afterwards, so unlike the two fields above it needs no atomicity. The
             * allocation must be Residency::Host (pinned host USM); the API that sets it
             * rejects a device-resident buffer at the call site.
             */
            const std::uint32_t* device_count = nullptr;

            /** @brief Element index of the live count within device_count. */
            std::size_t device_count_index = 0;

            /**
             * @brief First element this step iterates; the kernel adds it to each index.
             *
             * count says how many, this says from where. Graph colouring is the
             * case that needs it: each colour is a contiguous slice of one
             * constraint buffer, and the slices shift every time the constraint set
             * changes, so a node pinned to a baked offset would need an
             * index-indirection buffer and an extra load per element to find its
             * own work. Zero for an ordinary node, which then iterates from the
             * start exactly as before.
             */
            std::atomic<std::size_t> base{0};

            /**
             * @brief Host-readable location a predecessor writes the live base into.
             *
             * The device-sourced counterpart of base, and the companion of
             * device_count: a pass that computes both a colour's offset and its
             * length on the device should be able to hand over both the same way.
             * Read by the kernel as device_count is, under the same host-USM
             * requirement. Null on every ordinary
             * node, in which case the atomic base above is used.
             */
            const std::uint32_t* device_base = nullptr;

            /** @brief Element index of the live base within device_base. */
            std::size_t device_base_index = 0;
        };

    } // namespace Graph
} // namespace SushiRuntime
