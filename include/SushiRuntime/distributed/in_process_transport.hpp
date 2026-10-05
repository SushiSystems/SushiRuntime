/****************************************************************/
/* in_process_transport.hpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file in_process_transport.hpp
 * @brief Declares InProcessFabric and InProcessTransport, an in-process simulated transport.
 * @author Mustafa Garip
 */

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
         * @brief Dictates how the transport simulation handles an in-flight message.
         *
         * Permits deterministic injection of network failures like packet loss or reordering.
         */
        enum class FaultAction
        {
            Deliver,    /**< Normal: route the message to its destination. */
            Drop,       /**< Silently discard it (a lost packet). */
            Duplicate,  /**< Deliver it twice (a spurious retransmit). */
            Hold        /**< Park it until flush_held(); models an arbitrarily slow peer. */
        };

        /**
         * @brief Callback determining the fault action for each transmitted message.
         *
         * @param from_peer The ID of the sending node.
         * @param to_peer The ID of the destination node.
         * @param type The kind of message being transmitted.
         * @return The action to apply to the message, defaulting to delivery.
         */
        using FaultPolicy =
            std::function<FaultAction(int from_peer, int to_peer, MsgType type)>;

        /**
         * @brief Simulates a complete cluster network within a single process.
         *
         * Connects local endpoints via asynchronous queues, providing a testbed for distributed logic without real networking.
         */
        class SUSHIRUNTIME_API InProcessFabric
        {
            public:
                /**
                 * @brief Constructs a new simulated network fabric.
                 *
                 * @param worker_count The number of simulated worker nodes to provision.
                 */
                explicit InProcessFabric(int worker_count);

                /**
                 * @brief Destroys the simulated network fabric.
                 *
                 * Cleans up shared routing state.
                 */
                ~InProcessFabric();

                InProcessFabric(const InProcessFabric&) = delete;
                InProcessFabric& operator=(const InProcessFabric&) = delete;

                /**
                 * @brief Retrieves the number of worker nodes managed by this fabric.
                 *
                 * @return The simulated worker count.
                 */
                int worker_count() const;

                /**
                 * @brief Configures the active failure injection strategy.
                 *
                 * Applied thread-safely to all subsequent messages. Passing nullptr disables fault injection.
                 *
                 * @param policy The fault decision callback to enforce.
                 */
                void set_fault_policy(FaultPolicy policy);

                /**
                 * @brief Delivers all messages currently detained by a hold policy.
                 *
                 * Emulates the sudden arrival of delayed packets, potentially scrambling ordering.
                 *
                 * @param shuffle_seed Seed for randomized delivery order; 0 preserves FIFO.
                 */
                void flush_held(uint64_t shuffle_seed = 0);

                /**
                 * @brief Counts the messages currently parked by the fault policy.
                 *
                 * @return The number of detained messages.
                 */
                std::size_t held_count() const;

                /**
                 * @brief Isolates or reconnects a worker to simulate network partitions.
                 *
                 * All traffic to and from the isolated worker is silently dropped.
                 *
                 * @param worker The target worker ID.
                 * @param partitioned True to sever connectivity, false to restore it.
                 */
                void set_partitioned(int worker, bool partitioned);

                struct Impl;
                /** @brief Internal shared routing state (public for endpoint access). */
                std::shared_ptr<Impl> impl_;
        };

        /**
         * @brief A single communication endpoint attached to an in-process fabric.
         *
         * Provides the standard transport interface over simulated local queues.
         */
        class SUSHIRUNTIME_API InProcessTransport final : public ITransport
        {
            public:
                /**
                 * @brief Constructs a simulated transport endpoint.
                 *
                 * @param fabric The shared routing switchboard to attach to.
                 * @param node_id The identity of this endpoint (-1 for master, 0..N-1 for workers).
                 */
                InProcessTransport(InProcessFabric& fabric, int node_id);

                /**
                 * @brief Destroys the simulated transport endpoint.
                 *
                 * Detaches from the shared fabric.
                 */
                ~InProcessTransport() override;

                /**
                 * @brief Transmits a framed message to a target peer via the simulated fabric.
                 *
                 * @param peer_id The destination node ID.
                 * @param type The semantic kind of the message.
                 * @param payload The serialized data to transmit.
                 */
                void send(int peer_id, MsgType type, SushiRuntime::span<const std::byte> payload) override;

                /**
                 * @brief Registers the callback responsible for handling all inbound messages.
                 *
                 * @param handler The function to invoke upon message receipt.
                 */
                void on_message(MessageHandler handler) override;

                /**
                 * @brief Activates the simulated transport endpoint.
                 *
                 * Spawns the local delivery thread to process the asynchronous incoming queue.
                 */
                void start() override;

                /**
                 * @brief Halts the simulated transport endpoint.
                 *
                 * Safely drains the local queue and terminates the delivery thread.
                 */
                void stop() override;

                /**
                 * @brief Retrieves the number of reachable endpoints on the fabric.
                 *
                 * @return The worker count for the master, or 1 for a worker node.
                 */
                int peer_count() const override;

                /** @brief Per-endpoint state; public so the fabric can hold weak refs to endpoints. */
                struct Impl;

            private:
                std::shared_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
