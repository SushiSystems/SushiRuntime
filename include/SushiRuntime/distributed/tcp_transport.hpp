/**************************************************************************/
/* tcp_transport.hpp                                                      */
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

#include <string>
#include <memory>
#include <cstdint>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Real TCP transport over raw sockets (POSIX + Winsock).
         *
         * Dependency-free — no Boost, no Asio, no ZeroMQ — which keeps the
         * "lightweight, no external DLL" goal intact and lets the lab's 5
         * machines talk directly. Wire framing matches the in-process fabric
         * (`[4B length][1B type][payload]`), so the coordinator and worker agent
         * are unchanged when swapping `InProcessTransport` for this.
         *
         * Asymmetric, mirroring the master-worker model:
         *   - Master binds a port and accepts `worker_count` connections; peer
         *     ids are assigned 0..N-1 in accept order. send(peer) targets that
         *     worker; peer < 0 broadcasts.
         *   - Worker connects to the master; send() always targets the master,
         *     and inbound messages are reported with peer == -1.
         *
         * One reader thread per connection; writes to a socket are serialized.
         * Homogeneous-architecture lab assumed (native-endian length prefix).
         */
        class SUSHIRUNTIME_API TcpTransport final : public ITransport
        {
            public:
                /**
                 * @brief Creates a master endpoint bound to @p port (0 = OS-assigned).
                 *
                 * Binds immediately so bound_port() is valid before start(); the
                 * actual accept loop runs once start() is called.
                 */
                static std::shared_ptr<TcpTransport> make_master(uint16_t port, int worker_count);

                /** @brief Creates a worker endpoint that will connect to @p host:@p port on start(). */
                static std::shared_ptr<TcpTransport> make_worker(const std::string& host, uint16_t port);

                ~TcpTransport() override;

                void send(int peer_id, MsgType type, std::span<const std::byte> payload) override;
                void on_message(MessageHandler handler) override;
                void start() override;
                void stop() override;
                int  peer_count() const override;

                /** @brief Actual bound port (master only); useful when port 0 was requested. */
                uint16_t bound_port() const;

                struct Impl;

            private:
                explicit TcpTransport(std::shared_ptr<Impl> impl);
                std::shared_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
