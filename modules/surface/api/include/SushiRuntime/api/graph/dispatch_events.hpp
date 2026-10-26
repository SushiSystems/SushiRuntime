/**************************************************************************/
/* dispatch_events.hpp                                                    */
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
 * @file dispatch_events.hpp
 * @brief Declares note_dispatch_event, which a task body calls for every event it submitted.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    /**
     * @brief Hands over one event a host-work body submitted, so its dispatch is timed across all of them.
     *
     * Call it once per submitted event. The returned event must complete after every one, since the
     * successors wait on it. A body that calls it never is timed by its returned event. Does nothing
     * outside a body or with no observer added.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    SUSHIRUNTIME_API void note_dispatch_event(const sycl::event& event);
} // namespace SushiRuntime
