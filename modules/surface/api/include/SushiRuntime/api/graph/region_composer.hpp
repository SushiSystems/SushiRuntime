/**************************************************************************/
/* region_composer.hpp                                                    */
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
 * @file region_composer.hpp
 * @brief Declares RegionComposer, which stitches the plan of a DynamicGraph from its regions.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>

#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct CompiledGraph;
    } // namespace Internal::Graph

    namespace Detail
    {
        class RegionComposerState;
    } // namespace Detail

    /**
     * @brief Owns the compiled sub-graph of every region and stitches them into one plan.
     *
     * A move leaves the source with no region and no plan, ready to compose again.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    class RegionComposer
    {
        public:
            /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
            using RegionKey = std::uint64_t;

            /**
             * @brief Rebuilds the changed regions, re-wires them and renumbers every node.
             * @param recorder_of Called as recorder_of(key); null when the region is gone.
             * @pre A commit that threw is repeated with the dirty and dropped keys it had.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            SUSHIRUNTIME_API void commit(
                const std::set<RegionKey>& dirty, const std::set<RegionKey>& dropped,
                const std::function<const Recorder*(RegionKey)>& recorder_of);

            /**
             * @brief Re-arms the live in-degree of every node and of the sentinel, in
             *        O(total nodes).
             * @pre composed() is true.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            SUSHIRUNTIME_API void rearm() noexcept;

            /** @brief Returns how many times the plan has been composed. */
            [[nodiscard]] SUSHIRUNTIME_API std::size_t compose_count() const noexcept;

            /**
             * @brief Returns whether a composed plan stands.
             * @return False before the first commit and after a commit that threw midway.
             */
            [[nodiscard]] SUSHIRUNTIME_API bool composed() const noexcept;

            /**
             * @brief Returns whether a region's cached sub-graph matches its recorder.
             * @return False for a region never built, or built at another generation.
             */
            [[nodiscard]] SUSHIRUNTIME_API bool is_current(RegionKey key,
                                                           const Recorder& recorder) const;

            /**
             * @brief Returns the registration index a rule of a region carries in the plan.
             * @param rule Position in the region's recording order, one per rule.
             * @return The index, or Detail::K_NO_REGISTRATION before the region is built.
             */
            [[nodiscard]] SUSHIRUNTIME_API std::uint32_t
            registration_of(RegionKey key, std::size_t rule) const noexcept;

        protected:
            /**
             * @brief Starts with no region and no plan.
             * @param profiling Whether the first plan requests per-node device timing.
             */
            SUSHIRUNTIME_API explicit RegionComposer(bool profiling) noexcept;

            /**
             * @brief Takes over another composer's regions and plan.
             * @param other Left with no region, no plan and a compose count of zero.
             */
            SUSHIRUNTIME_API RegionComposer(RegionComposer&& other) noexcept;

            /**
             * @brief Replaces this composer's regions and plan with another's.
             * @param other Left with no region, no plan and a compose count of zero.
             */
            SUSHIRUNTIME_API RegionComposer& operator=(RegionComposer&& other) noexcept;

            RegionComposer(const RegionComposer&) = delete;
            RegionComposer& operator=(const RegionComposer&) = delete;

            /** @brief Frees every region's sub-graph and the plan. */
            SUSHIRUNTIME_API ~RegionComposer();

            /**
             * @brief Requests or withdraws per-node device timing.
             *
             * The next commit stamps the request into the plan; the same value changes
             * nothing.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void set_profiling(bool requested) noexcept
            {
                profiling_requested_ = requested;
            }

            /**
             * @brief Returns whether the next plan requests per-node device timing.
             * @return The last value given to set_profiling(), or the value the composer
             *         was constructed with.
             */
            [[nodiscard]] bool is_profiling_requested() const noexcept
            {
                return profiling_requested_;
            }

            /** @brief Returns whether the stitched plan has a node to start from. */
            [[nodiscard]] SUSHIRUNTIME_API bool has_roots() const noexcept;

            /**
             * @brief Returns the stitched plan; the owning graph and its test seams read it.
             * @pre commit() has been called since construction or the last move from here.
             */
            [[nodiscard]] SUSHIRUNTIME_API Internal::Graph::CompiledGraph& compiled_plan() noexcept;

            /**
             * @brief Returns the stitched plan, read-only.
             * @pre commit() has been called since construction or the last move from here.
             */
            [[nodiscard]] SUSHIRUNTIME_API const Internal::Graph::CompiledGraph&
            compiled_plan() const noexcept;

        private:
            /** @brief The regions, the edges between them and the plan; null until a commit. */
            std::unique_ptr<Detail::RegionComposerState> state_;

            /** @brief Starts as the runtime's profiling setting; stamped into the plan at commit. */
            bool profiling_requested_ = false;
    };
} // namespace SushiRuntime
