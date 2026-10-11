/**************************************************************************/
/* topology_view.cpp                                                      */
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
 * @file topology_view.cpp
 * @brief Implements TopologyView.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/topology_view.hpp>

#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>

#include <SushiRuntime/topology/topology_provider.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Reads the core map from the provider once and keeps the distance model.
         * @param provider Queried during construction only; not stored.
         * @param distance_model Shared with the view; must not be null.
         */
        TopologyView::TopologyView(const TopologyProvider& provider,
                                   std::shared_ptr<const NUMADistanceModel> distance_model)
            : core_to_numa_(provider.cpu_core_to_numa()),
              distance_model_(std::move(distance_model))
        {
            SR_VERIFY(distance_model_ != nullptr,
                      "TopologyView: the distance model is null; pass a model such as "
                      "HwlocDistanceModel or MatrixDistanceModel.");
        }

        /**
         * @brief Returns the NUMA node of each logical core, indexed by OS cpu index.
         */
        const std::vector<int>& TopologyView::core_to_numa() const noexcept
        {
            return core_to_numa_;
        }

        /**
         * @brief Returns the distance model's cost between two NUMA nodes.
         * @param numa_a Passed to the model unchecked.
         * @param numa_b Passed to the model unchecked.
         */
        int TopologyView::numa_distance(int numa_a, int numa_b) const
        {
            return distance_model_->distance(numa_a, numa_b);
        }

    } // namespace Topology
} // namespace SushiRuntime
