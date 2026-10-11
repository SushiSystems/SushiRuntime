/**************************************************************************/
/* live_fields.hpp                                                        */
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
 * @file live_fields.hpp
 * @brief Declares LiveFields and with_live_fields, which let a State rule that names an
 *        operation list the fields it reads and writes at each step.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Holds the addresses a State rule reads and writes at its next step.
         *
         * Both lists are null for a rule that names no operation.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct LiveFields
        {
            /** @brief The current field of each State the rule reads, the one written first. */
            std::shared_ptr<std::vector<void*>> reads;

            /** @brief The next field of the State the rule writes. */
            std::shared_ptr<std::vector<void*>> writes;
        };

        /**
         * @brief Wraps the replay of a State rule so that its node lists live fields.
         * @param emit Replay callable invoked as emit(TaskGraph&, registration); it adds one
         *        node.
         * @param live The lists the node takes; with null lists the node is left as emit
         *        added it.
         */
        template <class Emit>
        auto with_live_fields(Emit&& emit, LiveFields live)
        {
            return [emit = std::forward<Emit>(emit), live = std::move(live)]
                   (::SushiRuntime::Internal::Graph::TaskGraph& backing_graph,
                    std::optional<std::uint32_t> registration)
            {
                emit(backing_graph, registration);
                if (live.reads != nullptr)
                    backing_graph.last_node_added()->set_live_io(live.reads, live.writes);
            };
        }
    } // namespace Detail
} // namespace SushiRuntime
