/**************************************************************************/
/* ready_sink.hpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <vector>

namespace SushiRuntime
{
    namespace Scheduler
    {
        class Task;

        /**
         * @brief Where a finishing task reports the work it just unblocked.
         *
         * This is the seam between the two halves of a completion. Deciding *which*
         * successors became runnable is the task's own business — only it knows how
         * its dependents are stored and what counts as satisfied. Making a runnable
         * task actually run is the scheduler's: the reference it must hold, the
         * in-flight count it must keep, the queue it must land on. Splitting them
         * here lets the scheduler drain a graph it cannot name the type of, which is
         * what keeps the layer boundary pointing one way.
         *
         * It is a buffer rather than a callback interface, and the difference is
         * measured, not stylistic. A virtual call per freed successor put an
         * un-inlinable barrier in the middle of the completion loop and cost 4.6x on
         * the wide fan-out benchmark (286 ns/task to 1303). Collecting into a plain
         * array leaves one virtual call per completion and lets the scheduler keep
         * pushing through its own fully inlined queue.
         *
         * The buffer belongs to the reaping worker and is cleared before each
         * completion, so it is never shared and needs no synchronisation.
         */
        using ReadySink = std::vector<Task*>;

    } // namespace Scheduler
} // namespace SushiRuntime
