/****************************************************************/
/* tcp_transport.hpp                                            */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
         * @brief Provides a TCP-based implementation for network communication using raw sockets.
         *
         * Establishes asymmetric master-worker connections to exchange framed messages across network boundaries.
         */
        class SUSHIRUNTIME_API TcpTransport final : public ITransport
        {
            public:
                /**
                 * @brief Instantiates a master network endpoint that listens for incoming worker connections.
                 *
                 * Pre-binds the socket to the specified port immediately, while connection acceptance is deferred until start() is invoked.
                 *
                 * @param port The port number to bind to, or 0 to let the operating system assign an available port.
                 * @param worker_count The exact number of worker connections to wait for.
                 * @return A shared pointer to the newly created transport instance.
                 * @throws std::runtime_error If socket creation or binding fails.
                 */
                static std::shared_ptr<TcpTransport> make_master(uint16_t port, int worker_count);

                /**
                 * @brief Instantiates a worker network endpoint configured to connect to a specific master.
                 *
                 * Stores the connection details for later use; the actual network connection is established when start() is invoked.
                 *
                 * @param host The hostname or IP address of the master node to connect to.
                 * @param port The destination port number on the master node.
                 * @return A shared pointer to the newly created transport instance.
                 */
                static std::shared_ptr<TcpTransport> make_worker(const std::string& host, uint16_t port);

                /**
                 * @brief Cleans up network resources and shuts down active connections.
                 *
                 * Ensures all underlying socket handles are properly closed and worker threads are joined.
                 */
                ~TcpTransport() override;

                /**
                 * @brief Transmits a binary payload to a specified peer.
                 *
                 * Frames the data with a header and writes it synchronously to the target socket.
                 *
                 * @param peer_id The numeric ID of the target peer, or < 0 to broadcast to all peers (master only).
                 * @param type The type of message being transmitted.
                 * @param payload A non-owning view of the raw bytes to send.
                 */
                void send(int peer_id, MsgType type, SushiRuntime::span<const std::byte> payload) override;

                /**
                 * @brief Registers a callback function to handle incoming network messages.
                 *
                 * Stores the provided handler to be invoked whenever a complete frame is received.
                 *
                 * @param handler The callback function to execute on message arrival.
                 */
                void on_message(MessageHandler handler) override;

                /**
                 * @brief Activates the transport layer to begin network operations.
                 *
                 * Starts background threads for accepting connections (master) or connecting and reading data (worker).
                 */
                void start() override;

                /**
                 * @brief Terminates all active network operations and gracefully stops background threads.
                 *
                 * Signals the internal loops to exit and closes open sockets to force unblocking.
                 */
                void stop() override;

                /**
                 * @brief Retrieves the total number of connected peers.
                 *
                 * Returns the count of established connections managed by this transport instance.
                 *
                 * @return The integer count of active peer connections.
                 */
                int  peer_count() const override;

                /**
                 * @brief Retrieves the actual local port number bound by a master endpoint.
                 *
                 * Queries the underlying socket address to determine the OS-assigned port if 0 was originally requested.
                 *
                 * @return The local port number in use.
                 * @warning This function is only valid for master endpoints; calling it on a worker yields undefined results.
                 */
                uint16_t bound_port() const;

                struct Impl;

            private:
                explicit TcpTransport(std::shared_ptr<Impl> impl);
                std::shared_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
