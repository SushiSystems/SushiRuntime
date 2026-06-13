/**************************************************************************/
/* i_transport.hpp                                                        */
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

#include <span>
#include <functional>
#include <cstddef>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/protocol.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Abstraction over the message-passing substrate. [DIP]
         *
         * Mirrors the topology layer's substitution pattern (Hwloc vs Remapping):
         *
         *   - AsioTcpTransport    -> real TCP between lab machines.
         *   - InProcessTransport  -> several "nodes" in one process over loopback
         *                            queues, so the whole master/worker protocol
         *                            can be developed and tested on a single
         *                            machine (and under WSL) with no network.
         *
         * The coordinator and worker agent depend only on this interface, so the
         * jump from single-machine simulation to a real cluster changes only the
         * endpoint list, never the upper layers.
         *
         * peer_id semantics: from the master, peer_id selects a worker
         * (0..N-1); from a worker, peer_id is ignored (it has one peer, the
         * master). Implementations may treat peer_id < 0 as "broadcast".
         */
        class SUSHIRUNTIME_API ITransport
        {
            public:
                /** @brief Callback signature for an inbound, fully-framed message. */
                using MessageHandler =
                    std::function<void(int peer, MsgType type, std::span<const std::byte> payload)>;

                virtual ~ITransport() = default;

                /**
                 * @brief Sends one framed message to a peer.
                 * @param peer_id Destination peer (worker index for master; ignored for worker).
                 * @param type Message kind.
                 * @param payload Already-serialized body bytes.
                 */
                virtual void send(int peer_id, MsgType type, std::span<const std::byte> payload) = 0;

                /** @brief Registers the handler invoked for every inbound message. */
                virtual void on_message(MessageHandler handler) = 0;

                /** @brief Begins serving (connect/accept, spin I/O threads). */
                virtual void start() = 0;

                /** @brief Stops serving and releases I/O resources. Idempotent. */
                virtual void stop() = 0;

                /** @brief Number of reachable peers (master: worker count; worker: 1). */
                virtual int peer_count() const = 0;
        };

    } // namespace Distributed
} // namespace SushiRuntime
