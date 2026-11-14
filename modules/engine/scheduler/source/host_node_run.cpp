/**************************************************************************/
/* host_node_run.cpp                                                      */
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
 * @file host_node_run.cpp
 * @brief Runs a host node inline and records its span under profiling.
 * @author Mustafa Garip
 */

#include "host_node_run.hpp"

#include <cstdint>

#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/observability/steady_time.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Runs a host node on the calling thread and, under profiling, records its span.
         */
        void run_host_node(Internal::Graph::Node& node, bool profiling, std::uint32_t step,
                           const std::atomic<Internal::Observability::ProfilingLedger*>& ledger,
                           Profile::Collector& collector)
        {
            if (!profiling)
            {
                node.execute_native();
                return;
            }

            const std::uint64_t h0 = Internal::Observability::steady_now_ns();
            {
                SR_ZONE_FINE(collector, "dispatch.host");
                node.execute_native();
            }
            const std::uint64_t h1 = Internal::Observability::steady_now_ns();

            // Raw steady times, on the host device; the observation carries them as they are.
            if (auto* active = ledger.load(std::memory_order_acquire))
            {
                // A host node submits no dispatch: the widths stay K_WIDTH_UNKNOWN and the device is the host.
                Internal::Observability::DispatchFacts facts;
                facts.node_index = node.registration_index;
                facts.invocation = step;
                facts.submit_thread = Internal::Profile::current_thread_index();
                active->record(facts, Internal::Observability::DispatchTimes{h0, h0, h1});
            }
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
