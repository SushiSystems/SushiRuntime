/**************************************************************************/
/* topology_view.hpp                                                      */
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
 * @file topology_view.hpp
 * @brief Declares TopologyView, the read-only core map and NUMA distances.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <memory>
#include <vector>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        class ITopologyProvider;
        class INumaDistanceModel;

        /**
         * @brief Holds the core-to-NUMA map and answers NUMA distance queries.
         * @see modules/platform/topology/README.md
         */
        class SUSHIRUNTIME_API TopologyView final
        {
            public:
                /**
                 * @brief Reads the core map from the provider once and keeps the distance model.
                 * @param provider Queried during construction only; not stored.
                 * @param distance_model Shared with the view; must not be null.
                 * @throws std::invalid_argument If distance_model is null.
                 */
                TopologyView(const ITopologyProvider& provider,
                             std::shared_ptr<const INumaDistanceModel> distance_model);

                /**
                 * @brief Returns the NUMA node of each logical core, indexed by OS cpu index.
                 * @return The map read at construction; empty when the provider supplies none.
                 */
                [[nodiscard]] const std::vector<int>& core_to_numa() const noexcept;

                /**
                 * @brief Returns the distance model's cost between two NUMA nodes.
                 * @param numa_a Passed to the model unchecked.
                 * @param numa_b Passed to the model unchecked.
                 * @return The model's answer, including its answer for ids it does not know.
                 */
                [[nodiscard]] int numa_distance(int numa_a, int numa_b) const;

            private:
                /// @brief NUMA node of each logical core, as the provider reported it.
                std::vector<int> core_to_numa_;
                /// @brief Distance model every numa_distance() call forwards to.
                std::shared_ptr<const INumaDistanceModel> distance_model_;
        };

    } // namespace Topology
} // namespace SushiRuntime
