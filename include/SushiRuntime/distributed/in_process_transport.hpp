/**************************************************************************/
/* in_process_transport.hpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief What a fault policy decides should happen to one in-flight message.
         *
         * Lets a test reproduce the partial-failure scenarios a real network
         * imposes — lost packets, retransmits, and a slow peer whose late reply
         * races a redispatch — all deterministically, on one machine.
         */
        enum class FaultAction
        {
            Deliver,    /**< Normal: route the message to its destination. */
            Drop,       /**< Silently discard it (a lost packet). */
            Duplicate,  /**< Deliver it twice (a spurious retransmit). */
            Hold        /**< Park it until flush_held(); models an arbitrarily slow peer. */
        };

        /**
         * @brief Decides the fate of each message; installed via set_fault_policy().
         *
         * @param from_peer Sender node id (-1 = master, 0..N-1 = worker).
         * @param to_peer   Destination node id (-1 = master, 0..N-1 = worker).
         * @param type      The message kind.
         * @return The action to take; default (no policy) is Deliver.
         */
        using FaultPolicy =
            std::function<FaultAction(int from_peer, int to_peer, MsgType type)>;

        /**
         * @brief Shared in-process switchboard connecting a master to N workers.
         *
         * Created once, then handed to each endpoint. Delivery happens on a
         * per-endpoint queue drained by its own thread, so message handling is
         * asynchronous and never reentrant — the same ordering and concurrency
         * shape a real socket transport would impose, minus the network. This is
         * the "RemappingTopologyProvider of the network layer": full multi-node
         * logic, single machine.
         *
         * For reliability testing it doubles as a fault injector: a FaultPolicy can
         * drop, duplicate, or hold messages so the master/worker recovery paths can
         * be driven through lost packets, retransmits, and slow-peer races without a
         * real network. Production code installs no policy and pays nothing.
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

                /**
                 * @brief Installs (or clears, with nullptr) the fault policy.
                 *
                 * Consulted for every message at send time. Thread-safe; may be
                 * swapped mid-run to change the failure scenario between phases.
                 */
                void set_fault_policy(FaultPolicy policy);

                /**
                 * @brief Delivers every message currently parked by FaultAction::Hold,
                 *        then clears the hold buffer.
                 *
                 * Models a slow peer's backlog finally draining — the moment a stale
                 * reply can race work that has already moved on. With a non-zero
                 * @p shuffle_seed the held messages are delivered in a seeded random
                 * order, injecting message reordering on top of the delay.
                 *
                 * @param shuffle_seed 0 = deliver in held (FIFO) order; else reorder.
                 */
                void flush_held(uint64_t shuffle_seed = 0);

                /** @brief Number of messages currently parked by Hold. */
                std::size_t held_count() const;

                /**
                 * @brief Partitions a worker off the network (or heals it).
                 *
                 * While partitioned, every message to or from @p worker is dropped in
                 * both directions — the classic split-brain nemesis. Healing it
                 * (partitioned=false) restores delivery for subsequent messages;
                 * anything sent during the partition is already lost (as on a real
                 * link). Out-of-range ids are ignored.
                 */
                void set_partitioned(int worker, bool partitioned);

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

                void send(int peer_id, MsgType type, SushiRuntime::span<const std::byte> payload) override;
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
