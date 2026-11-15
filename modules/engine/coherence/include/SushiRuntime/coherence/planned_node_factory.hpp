/**************************************************************************/
/* planned_node_factory.hpp                                               */
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
 * @file planned_node_factory.hpp
 * @brief Declares PlannedNodeFactory, the port that turns a planned step or effect into a graph node.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/coherence/copy_plan.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Builds the graph node that carries out one planned step or effect. */
        class PlannedNodeFactory
        {
            public:
                virtual ~PlannedNodeFactory() = default;

                /**
                 * @brief Returns a node, with no edge yet, that carries out step when it runs.
                 * @return The node, or the error that keeps the step from being built.
                 */
                [[nodiscard]] virtual Result<sushi_ptr<Graph::Node>> make(const PlannedStep& step) = 0;

                /**
                 * @brief Returns a node, with no edge yet, that applies effect to the map when it runs.
                 * @return The node, or the error that keeps the effect from being built.
                 */
                [[nodiscard]] virtual Result<sushi_ptr<Graph::Node>> make_effect(const PlannedEffect& effect) = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
