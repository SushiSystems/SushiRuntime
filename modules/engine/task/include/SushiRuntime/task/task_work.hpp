/**************************************************************************/
/* task_work.hpp                                                          */
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
 * @file task_work.hpp
 * @brief Declares TaskWork and HostWork, the callables a node runs.
 * @author Mustafa Garip
 */

#pragma once

#include <functional>
#include <vector>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /** @brief Work definition for a task node. */
        using TaskWork = std::function<void(sycl::handler&)>;
        
        /** @brief Work definition for a host-side library call node. */
        using HostWork = std::function<sycl::event(sycl::queue&, const std::vector<sycl::event>&)>;

    } // namespace Graph
} // namespace SushiRuntime
