/****************************************************************/
/* ready_sink.hpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file ready_sink.hpp
 * @brief Declares ReadySink, the buffer a finishing task reports unblocked work into.
 * @author Mustafa Garip
 * @see include/SushiRuntime/scheduler/README.md
 */

#pragma once

#include <vector>

namespace SushiRuntime
{
    namespace Scheduler
    {
        class Task;

        /**
         * @brief Names the buffer where a finishing task reports the work it unblocked.
         * @see include/SushiRuntime/scheduler/README.md
         */
        using ReadySink = std::vector<Task*>;

    } // namespace Scheduler
} // namespace SushiRuntime
