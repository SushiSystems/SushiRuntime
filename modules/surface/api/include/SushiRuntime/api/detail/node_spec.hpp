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
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include <SushiRuntime/api/graph/backing_graph.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Holds one recorded node: its replay callable, its name and its rule.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            struct NodeSpec
            {
                /** @brief Marks a node recorded outside every rule scope. */
                static constexpr std::uint32_t K_NO_RULE = static_cast<std::uint32_t>(-1);

                /**
                 * @brief Wraps a callable invoked as emit(BackingGraph&, registration) per replay.
                 * @param f The replay callable; it adds the node and stamps the registration given.
                 */
                template <class F,
                          std::enable_if_t<!std::is_same<std::decay_t<F>, NodeSpec>::value,
                                           int> = 0>
                explicit NodeSpec(F&& f) : emit(std::forward<F>(f))
                {
                }

                /** @brief Adds this node to a backing graph; no registration draws a fresh one. */
                std::function<void(BackingGraph&, std::optional<std::uint32_t>)> emit;

                /** @brief Label stamped on the node at replay; borrowed, or null for none. */
                const char* name = nullptr;

                /** @brief Rule scope this node was recorded in, or K_NO_RULE. */
                std::uint32_t rule = K_NO_RULE;
            };
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
