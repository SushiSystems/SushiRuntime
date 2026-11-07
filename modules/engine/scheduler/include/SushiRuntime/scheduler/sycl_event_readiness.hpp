/**************************************************************************/
/* sycl_event_readiness.hpp                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file sycl_event_readiness.hpp
 * @brief Declares SYCLEventReadiness, the EventReadiness that asks the SYCL runtime.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <sycl/sycl.hpp>

#include <SushiRuntime/scheduler/event_readiness.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Reads an event's execution status and waits on it through SYCL.
         * @see modules/engine/scheduler/README.md
         */
        class SYCLEventReadiness final : public EventReadiness
        {
            public:
                [[nodiscard]] bool is_complete(const sycl::event& event) override
                {
                    return event.get_info<sycl::info::event::command_execution_status>() ==
                           sycl::info::event_command_status::complete;
                }

                void wait(sycl::event& event) override
                {
                    event.wait();
                }
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
