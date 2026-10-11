/**************************************************************************/
/* node_spec.hpp                                                          */
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
 * @file node_spec.hpp
 * @brief Declares NodeSpec, one recorded node of a Recorder.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstdint>
#include <utility>

#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/detail/node_stamp.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Holds one recorded node: its description, its stamp and the rule it belongs
         *        to.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct NodeSpec
        {
            /** @brief Stands for "no rule": the value a replay starts from. */
            static constexpr std::uint32_t K_NO_RULE = static_cast<std::uint32_t>(-1);

            /** @brief Takes the description of one node. */
            explicit NodeSpec(NodeDescription&& described)
                : description(std::move(described))
            {
            }

            /** @brief What the library adds to a plan for this node at every build. */
            NodeDescription description;

            /** @brief What the caller stated about this node; a build writes it on the node. */
            NodeStamp stamp;

            /** @brief Position of this node's rule in the recorder's rule order. */
            std::uint32_t rule = K_NO_RULE;
        };
    } // namespace Detail
} // namespace SushiRuntime
