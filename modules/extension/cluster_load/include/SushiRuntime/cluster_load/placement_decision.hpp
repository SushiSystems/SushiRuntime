/**************************************************************************/
/* placement_decision.hpp                                                 */
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
 * @file placement_decision.hpp
 * @brief Declares PlacementDecision, where one offloadable task goes at dispatch.
 * @author Mustafa Garip
 * @see modules/extension/cluster_load/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <variant>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Sends the task to a worker, to run on one of its devices.
         */
        struct RunOnWorker
        {
            int worker_id = -1;           /**< The worker's id: the value of its PeerID. */
            std::size_t device_index = 0; /**< The worker's device, as TASK_DISPATCH carries it. */

            /** @brief Replaces the cluster's task deadline for this dispatch; none keeps it. */
            std::optional<std::chrono::milliseconds> deadline;
        };

        /**
         * @brief Runs the task on the master, in the pool that offered it.
         */
        struct RunLocally
        {

        };

        /**
         * @brief Holds the task until a worker registers or frees a slot, or a time passes.
         */
        struct WaitForTarget
        {
            /** @brief The end of the wait; time_point::max() while the target is only busy. */
            std::chrono::steady_clock::time_point until;
        };

        /**
         * @brief Ends the run, since the task's target is absent and its rule says Fail.
         */
        struct FailRun
        {
            Error error; /**< Names the rule and its target. */
        };

        /**
         * @brief Names where one offloadable task goes; the dispatcher visits it in one place.
         * @see modules/extension/cluster_load/README.md
         */
        using PlacementDecision = std::variant<RunOnWorker, RunLocally, WaitForTarget, FailRun>;

    } // namespace Distributed
} // namespace SushiRuntime
