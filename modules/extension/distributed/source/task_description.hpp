/**************************************************************************/
/* task_description.hpp                                                   */
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
 * @file task_description.hpp
 * @brief Declares describe_task, which states a node's task as the NodeOptions a kernel and a
 *        policy are handed.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Returns how many leading parameters reach the last one that is not zero. */
        [[nodiscard]] inline std::size_t
        used_parameter_count(const Internal::Graph::TaskMetadata& metadata) noexcept
        {
            std::size_t count = Internal::Graph::TaskMetadata::K_MAX_PARAMS;
            while (count > 0 && metadata.params[count - 1] == 0)
                --count;
            return count;
        }

        /**
         * @brief Returns the name, the operation and the used parameters of a node's task.
         * @return A description that borrows from @p metadata and is used while it lives.
         */
        [[nodiscard]] inline NodeOptions
        describe_task(const Internal::Graph::TaskMetadata& metadata) noexcept
        {
            NodeOptions options;
            if (metadata.name != nullptr)
                options.name = metadata.name;
            options.operation = metadata.op_id;
            options.parameters = span<const std::uint64_t>(metadata.params,
                                                           used_parameter_count(metadata));
            return options;
        }
    } // namespace Distributed
} // namespace SushiRuntime
