/**************************************************************************/
/* node_options.hpp                                                       */
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
 * @file node_options.hpp
 * @brief Declares NodeOptions, the optional settings of one recorded node.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/observability/dispatch_width.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Holds what a caller may state about a node beyond its launch and its access.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        struct NodeOptions
        {
            /** @brief Launch width stamped into the node's event records. */
            std::size_t width = Core::WIDTH_UNKNOWN;
        };

    } // namespace API
} // namespace SushiRuntime
