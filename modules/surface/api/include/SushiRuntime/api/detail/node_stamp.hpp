/**************************************************************************/
/* node_stamp.hpp                                                         */
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
 * @file node_stamp.hpp
 * @brief Declares NodeStamp, what a recorded node keeps of its NodeOptions.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief How many parameters one node carries. */
        inline constexpr std::size_t K_MAX_NODE_PARAMETERS = 8;

        /**
         * @brief Returns the refusal of more parameters than a node carries.
         * @return An InvalidGraph error naming the rule, or nothing for at most
         *         K_MAX_NODE_PARAMETERS.
         */
        [[nodiscard]] inline std::optional<Error> parameters_refusal(const NodeOptions& options,
                                                                     std::size_t rule)
        {
            const std::size_t given = options.parameters.size();
            if (given <= K_MAX_NODE_PARAMETERS)
                return std::nullopt;
            return Error{
                ErrorCode::InvalidGraph,
                "NodeOptions::parameters, rule " + std::to_string(rule) + ": " +
                    std::to_string(given) + " parameters were given and a node carries at most " +
                    std::to_string(K_MAX_NODE_PARAMETERS) + "; pass at most that many."};
        }

        /**
         * @brief Holds what a recorded node keeps of its NodeOptions, with nothing borrowed
         *        from the caller.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct NodeStamp
        {
            /** @brief Builds the stamp that states nothing. */
            NodeStamp() noexcept = default;

            /**
             * @brief Copies the operation, the width and the parameters of options.
             * @param owned_name The recorder's own copy of options.name, or null for none;
             *        it outlives every plan built from the recorder.
             * @pre parameters_refusal() returned nothing for options.
             */
            NodeStamp(const NodeOptions& options, const char* owned_name) noexcept
                : name(owned_name), operation(options.operation), width(options.width),
                  parameter_count(options.parameters.size())
            {
                for (std::size_t i = 0; i < parameter_count; ++i)
                    parameters[i] = options.parameters[i];
            }

            /** @brief Label the recorder owns, or null when the caller gave none. */
            const char* name = nullptr;

            /** @brief Operation the caller named; a default id names none. */
            OperationID operation;

            /** @brief Launch width the caller stated, or K_WIDTH_UNKNOWN. */
            std::size_t width = ::SushiRuntime::K_WIDTH_UNKNOWN;

            /** @brief The first parameter_count entries are the caller's parameters. */
            std::array<std::uint64_t, K_MAX_NODE_PARAMETERS> parameters{};

            /** @brief How many entries of parameters the caller gave. */
            std::size_t parameter_count = 0;
        };
    } // namespace Detail
} // namespace SushiRuntime
