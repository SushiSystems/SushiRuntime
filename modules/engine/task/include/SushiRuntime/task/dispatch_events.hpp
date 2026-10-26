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
 * @brief Declares DispatchEventScope, which collects the events a task body hands over for its timing.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <vector>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Internal::DispatchEvents
    {
        /**
         * @brief Collects, on the calling thread, the events a task body hands over with note().
         *
         * The dispatcher opens one around a body that records timing; the dispatch is then timed
         * from the earliest start to the latest end of them. Scopes nest.
         * @see modules/engine/task/README.md
         */
        class DispatchEventScope final
        {
            public:
                /** @brief Starts collecting into @p sink, which outlives the scope. */
                explicit DispatchEventScope(std::vector<sycl::event>& sink) noexcept
                    : previous_(current())
                {
                    current() = &sink;
                }

                DispatchEventScope(const DispatchEventScope&) = delete;
                DispatchEventScope& operator=(const DispatchEventScope&) = delete;
                DispatchEventScope(DispatchEventScope&&) = delete;
                DispatchEventScope& operator=(DispatchEventScope&&) = delete;

                /** @brief Restores the scope that was open before this one. */
                ~DispatchEventScope()
                {
                    current() = previous_;
                }

                /** @brief Hands @p event to the open scope of this thread; does nothing when none is open. */
                static void note(const sycl::event& event)
                {
                    if (std::vector<sycl::event>* sink = current())
                        sink->push_back(event);
                }

            private:
                /** @brief Returns the slot that holds this thread's open sink. */
                static std::vector<sycl::event>*& current() noexcept
                {
                    static thread_local std::vector<sycl::event>* sink = nullptr;
                    return sink;
                }

                /** @brief The sink that was open when this scope started. */
                std::vector<sycl::event>* previous_;
        };
    } // namespace DispatchEvents
} // namespace SushiRuntime
