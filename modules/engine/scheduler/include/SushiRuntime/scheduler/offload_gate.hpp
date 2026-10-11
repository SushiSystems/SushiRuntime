/**************************************************************************/
/* offload_gate.hpp                                                       */
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
 * @file offload_gate.hpp
 * @brief Declares OffloadGate, which offers a dispatched node to the offload coordinator.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <SushiRuntime/scheduler/run_outcome.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Offers each offloadable node a worker dispatches to the installed coordinator.
         * @see modules/engine/scheduler/README.md
         */
        class OffloadGate final
        {
            public:
                /**
                 * @brief Installs the coordinator nodes are offered to.
                 * @pre The pool runs no node.
                 * @param coordinator Not owned; null removes the installed one.
                 */
                void install(Internal::Graph::OffloadCoordinator* coordinator) noexcept
                {
                    coordinator_ = coordinator;
                }

                /**
                 * @brief Lets the coordinator mark the nodes of a compiled graph it may take.
                 * @pre None of the nodes has been submitted.
                 * @param nodes Borrowed for the call; with no coordinator nothing happens.
                 */
                void prepare(span<const sushi_ptr<Internal::Graph::Node>> nodes) const
                {
                    if (coordinator_ != nullptr)
                        coordinator_->prepare(nodes);
                }

                /**
                 * @brief Offers @p task to the coordinator.
                 * @param outcome A node of an aborting run is not offered; it stays local.
                 * @return True when the coordinator took the node and now owns it.
                 */
                [[nodiscard]] bool offer(Task& task, const RunOutcome& outcome) const
                {
                    if (coordinator_ == nullptr || !task.is_graph_node())
                        return false;

                    Internal::Graph::Node& node = static_cast<Internal::Graph::Node&>(task);
                    const Internal::Graph::OffloadState* state = node.offload_state();
                    if (state == nullptr || !state->offloadable)
                        return false;

                    if (outcome.is_aborting())
                        return false;

                    return coordinator_->try_offload(node);
                }

            private:
                /// @brief Coordinator nodes are offered to; null when none is installed.
                Internal::Graph::OffloadCoordinator* coordinator_ = nullptr;
        };

    } // namespace Scheduler
} // namespace SushiRuntime
