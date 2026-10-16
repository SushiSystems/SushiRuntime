/**************************************************************************/
/* worker_main.cpp                                                        */
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
 * @file worker_main.cpp
 * @brief Implements run_worker(): the command line, the secret and a TCP session.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/worker_main.hpp>

#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/version.hpp>
#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/distributed/distributed_log_category.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/network_socket/socket.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/process_environment/environment.hpp>
#include <SushiRuntime/process_environment/stop_signal.hpp>

#include <SushiRuntime/distributed/worker_options_parser.hpp>
#include "worker_session.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the name of a worker whose machine names itself nowhere. */
            constexpr const char* K_FALLBACK_NAME = "worker";

            /** @brief Pairs a log level's name on the command line with the level. */
            struct LogLevelName
            {
                /** @brief The name as typed. */
                std::string_view name;

                /** @brief The level it selects. */
                LogLevel level;
            };

            /** @brief Lists the names `--log-level` takes. */
            constexpr std::array<LogLevelName, 7> K_LOG_LEVEL_NAMES = {{
                {"trace", LogLevel::Trace},
                {"debug", LogLevel::Debug},
                {"info", LogLevel::Info},
                {"warn", LogLevel::Warn},
                {"error", LogLevel::Error},
                {"fatal", LogLevel::Fatal},
                {"off", LogLevel::Off},
            }};

            /** @brief Returns the process's exit value for a code. */
            [[nodiscard]] constexpr int exit_value(WorkerExitCode code) noexcept
            {
                return static_cast<int>(code);
            }

            /** @brief Writes one line about the command line to the error stream. */
            void report(const std::string& line)
            {
                std::fprintf(stderr, "sushiruntime worker: %s\n", line.c_str());
            }

            /** @brief Returns the level a name selects, or nothing for an unknown name. */
            [[nodiscard]] std::optional<LogLevel> find_log_level(std::string_view name)
            {
                for (const LogLevelName& entry : K_LOG_LEVEL_NAMES)
                {
                    if (entry.name == name)
                        return entry.level;
                }
                return std::nullopt;
            }

            /**
             * @brief Reads the cluster secret a worker's options name.
             * @return False, with the reason in @p problem, when the file is unreadable or its
             *         length is refused; an empty path gives an empty secret.
             */
            [[nodiscard]] bool load_secret(const std::filesystem::path& path, SharedSecret& secret,
                                           std::string& problem)
            {
                if (path.empty())
                    return true;

                Result<SharedSecret> loaded = SharedSecret::from_file(path);
                if (!loaded)
                {
                    problem = std::move(loaded.error().message);
                    return false;
                }
                secret = std::move(*loaded);
                return true;
            }

            /** @brief Returns the name a worker takes when its options give none. */
            [[nodiscard]] std::string default_worker_name()
            {
                std::string name = host_name();
                return name.empty() ? std::string(K_FALLBACK_NAME) : name;
            }

            /** @brief Returns the settings of the agent a worker process runs. */
            [[nodiscard]] WorkerAgentOptions make_agent_options(const WorkerOptions& options,
                                                                SharedSecret secret)
            {
                WorkerAgentOptions agent;
                agent.secret = std::move(secret);
                agent.name = options.name.empty() ? default_worker_name() : options.name;
                agent.max_jobs = options.max_jobs;
                agent.cache_bytes = options.cache_bytes;
                agent.heartbeat_interval = options.heartbeat_interval;
                agent.pull_idle_timeout = options.pull_idle_timeout;
                return agent;
            }

            /** @brief Returns the settings of the transport a worker process connects with. */
            [[nodiscard]] TCPWorkerOptions make_transport_options(const WorkerOptions& options)
            {
                TCPWorkerOptions transport;
                transport.connect_timeout = options.connect_timeout;
                transport.reconnect = options.reconnect;
                return transport;
            }

            /**
             * @brief Serves the master over TCP until the stop flag is set or the agent ends.
             * @return The exit code of the end; the agent is stopped when this returns.
             */
            [[nodiscard]] WorkerExitCode serve(const WorkerOptions& options, SharedSecret secret,
                                               sushi_ptr<KernelRegistry> kernels)
            {
                const StopSignal stop_signal;
                Result<Runtime> created = Runtime::create();
                if (!created)
                {
                    SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "worker has no runtime: {}; exiting with code {}",
                                 created.error().message,
                                 exit_value(WorkerExitCode::InternalError));
                    return WorkerExitCode::InternalError;
                }

                SR_LOG_INFO(K_DISTRIBUTED_LOG_CATEGORY, "worker joining master {}, protocol v{}, authentication {}",
                            options.master.to_string(), Wire::K_PROTOCOL_VERSION,
                            secret.empty() ? "off" : "on");

                return serve_worker(*created,
                                    TCPTransport::make_worker(options.master,
                                                              make_transport_options(options)),
                                    std::move(kernels),
                                    make_agent_options(options, std::move(secret)), stop_signal);
            }
        } // namespace

        int run_worker(int argc, char** argv, sushi_ptr<KernelRegistry> kernels)
        {
            WorkerOptionsResult parsed;
            try
            {
                const std::size_t count = argc > 1 ? static_cast<std::size_t>(argc - 1) : 0;
                parsed = parse_worker_options(
                    SushiRuntime::span<const char* const>(count != 0 ? argv + 1 : nullptr, count),
                    read_environment);
            }
            catch (const std::exception& error)
            {
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "worker could not read its command line: {}; exiting with code {}",
                             error.what(), exit_value(WorkerExitCode::InternalError));
                return exit_value(WorkerExitCode::InternalError);
            }

            if (parsed.status == WorkerOptionsStatus::Help)
            {
                std::fputs(parsed.message.c_str(), stdout);
                return exit_value(WorkerExitCode::StoppedBySignal);
            }

            if (parsed.status == WorkerOptionsStatus::Version)
            {
                std::printf("sushiruntime worker, protocol v%u, build %s\n",
                            static_cast<unsigned>(Wire::K_PROTOCOL_VERSION),
                            runtime_version_string());
                return exit_value(WorkerExitCode::StoppedBySignal);
            }

            if (parsed.status == WorkerOptionsStatus::Invalid)
            {
                report(parsed.message);
                return exit_value(WorkerExitCode::BadArguments);
            }

            return run_worker(parsed.options, std::move(kernels));
        }

        int run_worker(const WorkerOptions& options, sushi_ptr<KernelRegistry> kernels)
        {
            try
            {
                if (!kernels)
                {
                    SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "run_worker() was given no kernel registry; exiting with code {}",
                                 exit_value(WorkerExitCode::InternalError));
                    return exit_value(WorkerExitCode::InternalError);
                }
                kernels->seal();

                if (!options.log_level.empty())
                {
                    const std::optional<LogLevel> level = find_log_level(options.log_level);
                    if (!level)
                    {
                        report("log level '" + options.log_level +
                               "' is not one of off, error, warn, info, debug");
                        return exit_value(WorkerExitCode::BadArguments);
                    }
                    set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, *level);
                }

                SharedSecret secret;
                std::string problem;
                if (!load_secret(options.secret_file, secret, problem))
                {
                    report(problem);
                    return exit_value(WorkerExitCode::BadArguments);
                }

                return exit_value(serve(options, std::move(secret), std::move(kernels)));
            }
            catch (const std::exception& error)
            {
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "worker for master {} failed: {}; exiting with code {}",
                             options.master.to_string(), error.what(),
                             exit_value(WorkerExitCode::InternalError));
            }
            catch (...)
            {
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "worker for master {} failed: unknown exception; exiting with "
                             "code {}",
                             options.master.to_string(),
                             exit_value(WorkerExitCode::InternalError));
            }
            return exit_value(WorkerExitCode::InternalError);
        }

    } // namespace Distributed
} // namespace SushiRuntime
