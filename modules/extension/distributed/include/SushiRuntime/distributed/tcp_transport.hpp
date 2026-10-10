/**************************************************************************/
/* tcp_transport.hpp                                                      */
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
 * @file tcp_transport.hpp
 * @brief Declares TcpTransport, the ITransport that carries frames over TCP.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/endpoint.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds the settings of a master-side TCP transport.
         */
        struct TcpMasterOptions
        {
            /**
             * @brief Holds the address to listen on; port 0 takes any free port.
             *
             * The default reaches this machine only. `0.0.0.0`, `::` or an interface address
             * must be written out, and Cluster::attach() then requires a secret.
             */
            Endpoint bind{"127.0.0.1", 0};

            /** @brief Holds the number of pending connections the OS may queue. */
            int backlog = 16;

            /** @brief Caps the open connections; one above the cap is refused as busy. */
            std::size_t max_peers = 64;

            /** @brief Caps the control frames queued per peer; send() then returns QueueFull. */
            std::size_t control_queue_frames = 4096;

            /** @brief Closes a peer whose socket takes no byte for this long; match liveness. */
            std::chrono::milliseconds write_idle_timeout{5000};
        };

        /**
         * @brief Holds the settings of a worker-side TCP transport.
         */
        struct TcpWorkerOptions
        {
            /** @brief Bounds one connection attempt to one resolved address. */
            std::chrono::milliseconds attempt_timeout{3000};

            /** @brief Bounds the wait for the first connection; 0 waits without a limit. */
            std::chrono::seconds connect_timeout{60};

            /** @brief Connects again after the master is lost; false ends at the first loss. */
            bool reconnect = true;

            /** @brief Holds the wait before the second attempt; it doubles up to the ceiling. */
            std::chrono::milliseconds backoff_initial{100};

            /** @brief Holds the longest wait between two attempts. */
            std::chrono::milliseconds backoff_ceiling{5000};

            /** @brief Caps the control frames queued for the master. */
            std::size_t control_queue_frames = 4096;

            /** @brief Closes the connection when the socket takes no byte for this long. */
            std::chrono::milliseconds write_idle_timeout{5000};
        };

        /**
         * @brief Carries frames over TCP, two threads per connection.
         *
         * A master accepts on one thread; a worker resolves and connects on one thread, again
         * after each loss while reconnect is set. Each connection has a reader thread, which
         * makes that peer's events, and a writer thread.
         * @see modules/extension/distributed/README.md
         */
        class SUSHIRUNTIME_API TcpTransport final : public ITransport
        {
            public:
                struct Impl;

                /**
                 * @brief Creates a master endpoint; nothing is bound until start().
                 * @return Never null.
                 */
                [[nodiscard]] static std::shared_ptr<TcpTransport> make_master(
                    TcpMasterOptions options = TcpMasterOptions());

                /**
                 * @brief Creates a worker endpoint; nothing connects until start().
                 * @param master Host name or address and port; resolved again on every attempt.
                 * @return Never null.
                 */
                [[nodiscard]] static std::shared_ptr<TcpTransport> make_worker(
                    Endpoint master, TcpWorkerOptions options = TcpWorkerOptions());

                /** @brief Stops the transport and closes its sockets. */
                ~TcpTransport() override;

                TcpTransport(const TcpTransport&) = delete;
                TcpTransport& operator=(const TcpTransport&) = delete;
                TcpTransport(TcpTransport&&) = delete;
                TcpTransport& operator=(TcpTransport&&) = delete;

                /** @brief Binds and listens, or starts the connector; see ITransport::start(). */
                [[nodiscard]] TransportResult start(TransportEvents& events) override;

                /** @brief Closes the listener and every connection and joins every thread. */
                void stop() noexcept override;

                /** @brief Queues a control frame on the peer's outbox. */
                [[nodiscard]] SendStatus send(PeerId peer, MessageType type,
                                              SushiRuntime::span<const std::byte> payload) override;

                /** @brief Adds a stream to the peer's outbox. */
                [[nodiscard]] SendStatus send_stream(PeerId peer,
                                                     std::unique_ptr<FrameSource> source) override;

                /** @brief Closes one connection; queued control frames get one second to leave. */
                void close_peer(PeerId peer) noexcept override;

                /**
                 * @brief Returns Loopback when the bind address, or the master's host, is a
                 *        loopback address or `localhost`; Network otherwise.
                 */
                [[nodiscard]] TransportExposure exposure() const noexcept override;

                /** @brief Returns the bytes read from and written to every socket. */
                [[nodiscard]] TransportStatistics statistics() const noexcept override;

                /**
                 * @brief Returns the address and the port a master listens on.
                 * @pre This is a master endpoint and start() returned Ok.
                 */
                [[nodiscard]] Endpoint local_endpoint() const;

            private:
                /** @brief Wraps the state a factory built. */
                explicit TcpTransport(std::unique_ptr<Impl> impl) noexcept;

                /** @brief Holds the role, the options, the peer table and the threads. */
                std::unique_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
