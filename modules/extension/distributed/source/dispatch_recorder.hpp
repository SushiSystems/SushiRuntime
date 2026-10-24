/**************************************************************************/
/* dispatch_recorder.hpp                                                  */
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
 * @file dispatch_recorder.hpp
 * @brief Declares DispatchRecorder, which records a batch's jobs as tasks and runs them.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <string>

#include <SushiRuntime/api/graph/graph.hpp>
#include <SushiRuntime/api/interop/interop.hpp>
#include <SushiRuntime/region/liveness.hpp>

#include "dispatch_job.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Records each job of a batch as one task over borrowed views of its pinned
         *        buffers, on the device it names, and runs the batch as one graph.
         *
         * One run orders the jobs that share a buffer and overlaps the rest. Only the exec
         * thread calls it.
         * @see modules/extension/distributed/README.md
         */
        class DispatchRecorder final
        {
            public:
                /**
                 * @brief Builds a recorder over an empty graph of the worker's runtime.
                 * @param interop The interop facade of that runtime.
                 */
                DispatchRecorder(Graph graph, Interop interop);

                DispatchRecorder(const DispatchRecorder&) = delete;
                DispatchRecorder& operator=(const DispatchRecorder&) = delete;
                DispatchRecorder(DispatchRecorder&&) = delete;
                DispatchRecorder& operator=(DispatchRecorder&&) = delete;

                /** @brief Drops the tasks of the batch before; called before the first record(). */
                void clear();

                /**
                 * @brief Records a job's task; a failure is kept in the entry.
                 * @param entry Must stay where it is until the next clear(): the task refers to it.
                 */
                void record(DispatchEntry& entry) noexcept;

                /**
                 * @brief Runs every task recorded since clear().
                 * @return Why the run failed, or empty when it ran or there was nothing to run.
                 */
                [[nodiscard]] std::string run();

            private:
                /// @brief Holds the tasks of the batch being run; cleared for each batch.
                Graph graph_;

                /// @brief Wraps the cache's memory as buffers the graph tracks.
                Interop interop_;

                /// @brief The owner every borrowed buffer names; the pins keep the memory.
                LivenessToken pinned_life_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
