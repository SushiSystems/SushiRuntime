/**************************************************************************/
/* dispatch_events.cpp                                                    */
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
 * @file dispatch_events.cpp
 * @brief Implements note_dispatch_event over the dispatcher's event scope.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#include <SushiRuntime/api/graph/dispatch_events.hpp>

#include <SushiRuntime/task/dispatch_events.hpp>

namespace SushiRuntime
{
    void note_dispatch_event(const sycl::event& event)
    {
        Internal::DispatchEvents::DispatchEventScope::note(event);
    }
} // namespace SushiRuntime
