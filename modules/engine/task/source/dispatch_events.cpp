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
 * @brief Defines the one thread-local sink slot every DispatchEventScope shares.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#include <SushiRuntime/task/dispatch_events.hpp>

namespace SushiRuntime
{
    namespace Internal::DispatchEvents
    {
        /**
         * @brief Returns the slot that holds this thread's open sink.
         */
        std::vector<sycl::event>*& DispatchEventScope::current() noexcept
        {
            static thread_local std::vector<sycl::event>* sink = nullptr;
            return sink;
        }

    } // namespace Internal::DispatchEvents
} // namespace SushiRuntime
