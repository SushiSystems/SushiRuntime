/**************************************************************************/
/* recorder_replay.hpp                                                    */
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
 * @file recorder_replay.hpp
 * @brief Declares RecorderReplay, which adds a Recorder's nodes to the engine's graph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <SushiRuntime/api/detail/node_spec.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/node.hpp>

#include "node_emitters.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Reads a Recorder's nodes for the plan builders of the library.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        struct RecorderReplay
        {
            /**
             * @brief Adds every recorded node to a backing graph, in recording order.
             *
             * The nodes of one rule share a registration index reserved on target. Each
             * carries what its NodeOptions stated, and the rule's name when it stated none.
             * @param visit Called as visit(rule, node) once node is added and named.
             */
            template <class Visit>
            static void replay(const Recorder& recorder, Internal::Graph::TaskGraph& target,
                               Visit&& visit)
            {
                std::uint32_t stamped = NodeSpec::K_NO_RULE;
                std::optional<std::uint32_t> reserved;
                for (std::size_t node = 0; node < recorder.specs_.size(); ++node)
                {
                    const NodeSpec& spec = recorder.specs_[node];
                    if (spec.rule != stamped)
                    {
                        stamped = spec.rule;
                        reserved = target.reserve_registration();
                    }
                    Internal::Graph::Node& added =
                        add_described_node(target, spec.description, reserved);
                    target.name_last_node(recorder.rule_names_[spec.rule]);
                    apply_stamp(spec.stamp, added);
                    visit(static_cast<std::size_t>(spec.rule), node);
                }
            }
        };
    } // namespace Detail
} // namespace SushiRuntime
