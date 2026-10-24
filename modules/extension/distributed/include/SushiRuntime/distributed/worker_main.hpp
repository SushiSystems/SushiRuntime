/**************************************************************************/
/* worker_main.hpp                                                        */
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
 * @file worker_main.hpp
 * @brief Declares run_worker(), the whole of a worker process behind a thin main().
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/machine/node_label.hpp>
#include <SushiRuntime/transport/endpoint.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names the value a worker process returns.
         */
        enum class WorkerExitCode : int
        {
            StoppedBySignal = 0,        /**< The first SIGINT, SIGTERM or SIGBREAK was served. */
            BadArguments = 1,           /**< A flag, the endpoint or the secret file is wrong. */
            MasterUnreachable = 2,      /**< No connection within the connect timeout. */
            RejectedAuthentication = 3, /**< The master refused the secret, or failed its proof. */
            ProtocolMismatch = 4,       /**< Another protocol version, or the peer is no master. */
            MasterLost = 5,             /**< The master went away and reconnecting is off. */
            InternalError = 70,         /**< An exception reached run_worker(). */
            SecondSignal = 130,         /**< A second signal ended the process at once. */
        };

        /**
         * @brief Holds the settings of a worker process, one field per command line flag.
         *
         * A flag beats its environment variable, which beats the default written here. An
         * application with flags of its own parses them, fills this and calls the overload of
         * run_worker() that takes it.
         */
        struct WorkerOptions
        {
            /** @brief `--master host:port`, `SR_MASTER`; required. */
            Endpoint master;

            /** @brief `--secret-file path`, `SR_CLUSTER_SECRET_FILE`; empty joins without one. */
            std::filesystem::path secret_file;

            /** @brief `--name text`, `SR_WORKER_NAME`; empty takes the host name. */
            std::string name;

            /** @brief `--max-jobs n`, `SR_WORKER_MAX_JOBS`. */
            std::size_t max_jobs = 1024;

            /** @brief `--cache-bytes n`, `SR_WORKER_CACHE_BYTES`; 0 takes half of device memory. */
            std::uint64_t cache_bytes = 0;

            /** @brief `--heartbeat-ms n`, `SR_WORKER_HEARTBEAT_MS`. */
            std::chrono::milliseconds heartbeat_interval{1000};

            /** @brief `--pull-idle-timeout-ms n`, `SR_WORKER_PULL_IDLE_TIMEOUT_MS`. */
            std::chrono::milliseconds pull_idle_timeout{5000};

            /** @brief `--connect-timeout-s n`, `SR_WORKER_CONNECT_TIMEOUT_S`; 0 waits forever. */
            std::chrono::seconds connect_timeout{60};

            /** @brief Cleared by `--no-reconnect`: the process then exits when the master goes. */
            bool reconnect = true;

            /** @brief `--log-level name`; empty leaves the process's log level alone. */
            std::string log_level;

            /**
             * @brief `--label key=value`, repeatable, and `SR_WORKER_LABELS`; sent to the master.
             *
             * The variable's labels come first, then the flags'. A key is not empty, holds no
             * '=' and appears once; the worker's runtime refuses a list that breaks this.
             */
            std::vector<NodeLabel> labels;
        };

        /**
         * @brief Runs a worker from its command line until a signal or a fatal end.
         *
         * Reads the flags and the environment, then calls the other overload. `--help` and
         * `--version` print and return 0; a bad or unknown flag prints the reason and returns 1.
         * @param kernels The kernels this binary can run; not null.
         * @return A WorkerExitCode value, for main() to return.
         */
        [[nodiscard]] SUSHIRUNTIME_API int run_worker(int argc, char** argv,
                                                      sushi_ptr<KernelRegistry> kernels);

        /**
         * @brief Runs a worker with settings the caller built; blocks the calling thread.
         *
         * Builds a runtime, a TCP transport and a WorkerAgent, installs the signal handlers
         * and polls them every 100 ms. No exception leaves the call.
         * @param kernels The kernels this binary can run; not null.
         * @return A WorkerExitCode value.
         */
        [[nodiscard]] SUSHIRUNTIME_API int run_worker(const WorkerOptions& options,
                                                      sushi_ptr<KernelRegistry> kernels);

    } // namespace Distributed
} // namespace SushiRuntime
