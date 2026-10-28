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
 * @file host_node_run.hpp
 * @brief Declares the inline run of a host node that submits no device work.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>

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
         */
        void run_host_node(Internal::Graph::Node& node, bool profiling, std::uint32_t step,
                           const std::atomic<Internal::Observability::ProfilingLedger*>& ledger);

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
