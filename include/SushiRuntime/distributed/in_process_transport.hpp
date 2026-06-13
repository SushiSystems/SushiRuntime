/**************************************************************************/
/* in_process_transport.hpp                                               */
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
#include <SushiRuntime/distributed/i_transport.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Shared in-process switchboard connecting a master to N workers.
         *
         * Created once, then handed to each endpoint. Delivery happens on a
         * per-endpoint queue drained by its own thread, so message handling is
         * asynchronous and never reentrant — the same ordering and concurrency
         * shape a real socket transport would impose, minus the network. This is
         * the "RemappingTopologyProvider of the network layer": full multi-node
         * logic, single machine.
         */
        class SUSHIRUNTIME_API InProcessFabric
        {
            public:
                explicit InProcessFabric(int worker_count);
                ~InProcessFabric();

                InProcessFabric(const InProcessFabric&) = delete;
                InProcessFabric& operator=(const InProcessFabric&) = delete;

                /** @brief Number of workers this fabric wires up. */
                int worker_count() const;

                struct Impl;
                /** @brief Internal shared routing state (public for endpoint access). */
                std::shared_ptr<Impl> impl_;
        };

        /**
         * @brief One ITransport endpoint on an InProcessFabric.
         *
         * node_id == -1 designates the master; 0..N-1 designate workers. From the
         * master, send(peer_id, ...) routes to worker peer_id (or broadcasts when
         * peer_id < 0); from a worker, the destination is always the master.
         */
        class SUSHIRUNTIME_API InProcessTransport final : public ITransport
        {
            public:
                InProcessTransport(InProcessFabric& fabric, int node_id);
                ~InProcessTransport() override;

                void send(int peer_id, MsgType type, std::span<const std::byte> payload) override;
                void on_message(MessageHandler handler) override;
                void start() override;
                void stop() override;
                int  peer_count() const override;

                /** @brief Per-endpoint state; public so the fabric can hold weak refs to endpoints. */
                struct Impl;

            private:
                std::shared_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
