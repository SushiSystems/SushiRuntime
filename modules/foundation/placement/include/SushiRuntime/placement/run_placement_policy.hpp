/**************************************************************************/
/* run_placement_policy.hpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file run_placement_policy.hpp
 * @brief Declares RunPlacementPolicy, the port a caller implements to place a graph's runs.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#pragma once

#include <SushiRuntime/placement/run_description.hpp>
#include <SushiRuntime/placement/run_placement.hpp>

namespace SushiRuntime
{
    /**
     * @brief Answers, for one run of a graph, whether its steps go to the device or the host.
     *
     * The caller implements it; a graph asks it once per blocking run, on the running thread.
     * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
     */
    class RunPlacementPolicy
    {
        public:
            virtual ~RunPlacementPolicy() = default;

            /**
             * @brief Returns where the steps of the run @p run describes go.
             * @param run Valid for the length of the call only.
             */
            [[nodiscard]] virtual RunPlacement place(const RunDescription& run) = 0;
    };
} // namespace SushiRuntime
