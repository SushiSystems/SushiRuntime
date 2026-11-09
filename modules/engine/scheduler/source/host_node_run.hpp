/**************************************************************************/
/* host_node_run.hpp                                                      */
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
 * @file host_node_run.hpp
 * @brief Declares the inline run of a host node that submits no device work.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>

#include <SushiRuntime/profile/profile.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }
    namespace Internal::Observability { class ProfilingLedger; }

    namespace Internal::Scheduler
    {
        /**
         * @brief Runs a host node on the calling thread and, under profiling, records its span.
         * @param node A host node whose submits_device_work() is false.
         * @param profiling The run's profiling flag, read once for the dispatch.
         * @param step The run step the dispatch belongs to; used only under profiling.
         * @param ledger Loaded after the node ran; the span is recorded when it holds a ledger.
         * @param collector Takes the dispatch zone.
         */
        void run_host_node(Internal::Graph::Node& node, bool profiling, std::uint32_t step,
                           const std::atomic<Internal::Observability::ProfilingLedger*>& ledger,
                           Profile::Collector& collector);

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
