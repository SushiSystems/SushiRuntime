/**************************************************************************/
/* worker_session.cpp                                                     */
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
 * @file worker_session.cpp
 * @brief Implements serve_worker() and the table from an agent's end to an exit code.
 * @author Mustafa Garip
 */

#include "worker_session.hpp"

#include <array>
#include <chrono>
#include <utility>

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/distributed/distributed_log_category.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds how often the serving thread looks at the stop flag. */
            constexpr std::chrono::milliseconds K_STOP_POLL{100};

            /** @brief Pairs the end of an agent with the exit code of its process. */
            struct EndExitCode
            {
                /** @brief How the agent ended. */
                WorkerEnd end;

                /** @brief What the process returns for it. */
                WorkerExitCode code;
            };

            /** @brief Lists every end; Running is the end of a wait the stop flag cut short. */
            constexpr std::array<EndExitCode, 6> K_EXIT_CODES = {{
                {WorkerEnd::Running, WorkerExitCode::StoppedBySignal},
                {WorkerEnd::Stopped, WorkerExitCode::StoppedBySignal},
                {WorkerEnd::MasterUnreachable, WorkerExitCode::MasterUnreachable},
                {WorkerEnd::RejectedAuthentication, WorkerExitCode::RejectedAuthentication},
                {WorkerEnd::ProtocolMismatch, WorkerExitCode::ProtocolMismatch},
                {WorkerEnd::MasterLost, WorkerExitCode::MasterLost},
            }};
        } // namespace

        WorkerExitCode exit_code_of(WorkerEnd end) noexcept
        {
            for (const EndExitCode& entry : K_EXIT_CODES)
            {
                if (entry.end == end)
                    return entry.code;
            }
            return WorkerExitCode::InternalError;
        }

        WorkerExitCode serve_worker(Runtime& runtime, std::shared_ptr<Transport> transport,
                                    sushi_ptr<KernelRegistry> kernels, WorkerAgentOptions options,
                                    const StopSignal& stop_signal)
        {
            WorkerAgent agent(runtime, std::move(transport), std::move(kernels),
                              std::move(options));

            const TransportResult started = agent.start();
            if (!started.is_ok())
            {
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "worker did not start: {}; exiting with code {}", started.message,
                             static_cast<int>(WorkerExitCode::InternalError));
                return WorkerExitCode::InternalError;
            }

            WorkerEnd end = WorkerEnd::Running;
            while (end == WorkerEnd::Running && !stop_signal.is_requested())
                end = agent.wait_for_end(K_STOP_POLL);

            agent.stop();
            return exit_code_of(end);
        }

    } // namespace Distributed
} // namespace SushiRuntime
