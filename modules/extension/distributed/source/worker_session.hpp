/**************************************************************************/
/* worker_session.hpp                                                     */
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
 * @file worker_session.hpp
 * @brief Declares serve_worker(), which runs one agent on a given transport until it ends.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/distributed/worker_main.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/process_environment/stop_signal.hpp>


namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Returns the exit code of the way an agent ended.
         *
         * Running is the end of a wait the stop flag cut short and gives StoppedBySignal.
         */
        [[nodiscard]] SUSHIRUNTIME_API WorkerExitCode exit_code_of(WorkerEnd end) noexcept;

        /**
         * @brief Serves the master behind @p transport until the stop flag is set or the
         *        agent ends; the agent is stopped when this returns.
         * @param runtime The runtime the kernels run on; it outlives the call.
         * @param transport A worker transport that was not started.
         * @param stop_signal The flag the loop reads every 100 ms.
         * @return InternalError when the agent does not start, else the code of its end.
         */
        [[nodiscard]] SUSHIRUNTIME_API WorkerExitCode
        serve_worker(Runtime& runtime, std::shared_ptr<Transport> transport,
                     sushi_ptr<KernelRegistry> kernels, WorkerAgentOptions options,
                     const StopSignal& stop_signal);

    } // namespace Distributed
} // namespace SushiRuntime
