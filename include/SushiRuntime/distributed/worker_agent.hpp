/**************************************************************************/
/* worker_agent.hpp                                                       */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once

#include <memory>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>

namespace SushiRuntime
{
    namespace Execution { class RuntimeContext; }

    namespace Distributed
    {
        /**
         * @brief Stateless executor daemon running on each worker machine.
         *
         * The agent is a thin network shell around a normal, local RuntimeContext.
         * Inside the worker, the existing NUMA-aware hierarchical stealing and the
         * intra-NUMA manager run unchanged — the agent merely feeds them work that
         * arrived over the wire.
         *
         * Cycle:
         *   1. HELLO the master with this binary's registered OpID set.
         *   2. On TASK_DISPATCH: resolve each input BufferHandle against the local
         *      cache (pull-on-miss via BUFFER_REQUEST), run the OpID's kernel on
         *      the local context, cache outputs under their new versions.
         *   3. Reply TASK_RESULT (lazy — output bytes travel only when the master
         *      later requests them as another task's input).
         *
         * The buffer cache is keyed by {id, version}: a dispatch referencing a
         * version the worker lacks triggers a pull, which is exactly how a
         * master-side write (version bump) invalidates a stale cache entry.
         */
        class SUSHIRUNTIME_API WorkerAgent
        {
            public:
                WorkerAgent(Execution::RuntimeContext& ctx,
                            std::shared_ptr<ITransport> transport,
                            sushi_ptr<KernelRegistry>   kernels);
                ~WorkerAgent();

                WorkerAgent(const WorkerAgent&) = delete;
                WorkerAgent& operator=(const WorkerAgent&) = delete;

                /** @brief Starts the transport and the message-processing loop. */
                void start();

                /** @brief Blocks until stop() is called from another thread. */
                void run();

                /** @brief Signals the agent to drain and stop. Safe from any thread. */
                void stop();

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
