/**************************************************************************/
/* offload_state.hpp                                                      */
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
 * @file offload_state.hpp
 * @brief Declares OffloadState, the per-node record of an offload coordinator.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace SushiRuntime
{
    class PlacementRules;

    namespace Internal::Graph
    {
        /**
         * @brief Bases the record a coordinator keeps on a node in OffloadState::payload.
         * @see modules/engine/task/README.md
         */
        class OffloadPayload
        {
            public:
                /** @brief Destroys the coordinator's record through OffloadState::payload. */
                virtual ~OffloadPayload() = default;

            protected:
                /** @brief Constructs the base of a coordinator's record. */
                OffloadPayload() = default;
        };

        /**
         * @brief Holds what a node carries for an offload coordinator; absent on most nodes.
         * @see modules/engine/task/README.md
         */
        struct OffloadState
        {
            /**
             * @brief Tells the scheduler to offer this node to the coordinator at dispatch.
             *
             * OffloadCoordinator::prepare() sets it, and clears it for a node it no longer
             * accepts.
             */
            bool offloadable = false;

            /** @brief Counts the dispatches to a worker in this run; the coordinator resets it. */
            std::atomic<std::uint32_t> attempts{0};

            /**
             * @brief Lists the addresses a double-buffered node reads at its next dispatch.
             *
             * Node::set_live_io() sets it; null on a node with fixed buffers.
             */
            std::shared_ptr<std::vector<void*>> live_reads;

            /** @brief Lists the addresses a double-buffered node writes; see live_reads. */
            std::shared_ptr<std::vector<void*>> live_writes;

            /** @brief Holds the coordinator's own record; OffloadCoordinator::prepare() sets it. */
            std::unique_ptr<OffloadPayload> payload;

            /**
             * @brief Holds the rule set the node's placement_rule points into; null for none.
             *
             * OffloadCoordinator::prepare() sets it from the rules of the node's own compile.
             */
            std::shared_ptr<const PlacementRules> rules;
        };

    } // namespace Graph
} // namespace SushiRuntime
