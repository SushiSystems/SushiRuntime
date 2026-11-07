/**************************************************************************/
/* offload_host.hpp                                                       */
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
 * @file offload_host.hpp
 * @brief Declares OffloadHost, what an engine that runs offload gives the context.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Internal::Graph { class OffloadCoordinator; class OffloadSink; }

    namespace Internal::Execution
    {
        /**
         * @brief Takes an offload coordinator, hands its nodes back, and holds its runs still for a
         *        change no run may see half done.
         *
         * SchedulerExecutionEngine implements it; an engine that runs no offload does not, and
         * RuntimeContext holds none for it.
         * @see modules/engine/execution/README.md
         */
        class OffloadHost
        {
            public:
                /** @brief Releases nothing. */
                virtual ~OffloadHost() = default;

                /**
                 * @brief Holds the engine idle for a change no run may see half done.
                 * @return False, holding nothing, when a run is in progress.
                 * @pre Exactly one end_idle_change() follows a true return.
                 */
                [[nodiscard]] virtual bool begin_idle_change() noexcept = 0;

                /** @brief Releases the hold a true begin_idle_change() took. */
                virtual void end_idle_change() noexcept = 0;

                /**
                 * @brief Hands the engine the offload coordinator its runs consult.
                 * @param offload Not owned; null removes the installed one.
                 * @pre No run is in flight.
                 */
                virtual void install_offload(Internal::Graph::OffloadCoordinator* offload) = 0;

                /** @brief Returns the sink an offload coordinator hands its nodes back to. */
                [[nodiscard]] virtual Internal::Graph::OffloadSink& offload_sink() noexcept = 0;

            protected:
                OffloadHost() = default;
                OffloadHost(const OffloadHost&) = default;
                OffloadHost& operator=(const OffloadHost&) = default;
                OffloadHost(OffloadHost&&) = default;
                OffloadHost& operator=(OffloadHost&&) = default;
        };
    } // namespace Internal::Execution
} // namespace SushiRuntime
