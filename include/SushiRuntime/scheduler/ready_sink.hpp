/****************************************************************/
/* ready_sink.hpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
