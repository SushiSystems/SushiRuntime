/**************************************************************************/
/* socket.hpp                                                             */
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
 * @file socket.hpp
 * @brief Declares Socket and Listener, the one place the module meets the OS socket interface.
 * @author Mustafa Garip
 * @see modules/extension/network_socket/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/endpoint.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Holds an OS socket: a descriptor on POSIX, a SOCKET value on Windows. */
        using NativeSocketHandle = std::intptr_t;

        /** @brief Handle of a socket that is not open, on every platform. */
        inline constexpr NativeSocketHandle K_INVALID_SOCKET_HANDLE = -1;

        /** @brief Timeout value that waits without a limit; any negative duration does. */
        inline constexpr std::chrono::milliseconds K_WAIT_FOREVER{-1};

        /**
         * @brief Names how a socket call ended.
         */
        enum class SocketStatus : std::uint8_t
        {
            Ok,              /**< The call did all it was asked. */
            Timeout,         /**< Nothing moved for the time the caller gave. */
            Closed,          /**< The peer ended the connection, or an earlier write failed. */
            ResolveFailed,   /**< The host name has no address. */
            InvalidArgument, /**< The caller's argument or the object's state rules the call out. */
            Failed,          /**< The OS refused the call; os_error says why. */
        };

        /**
         * @brief Returns the words describe() uses for a status that has no OS text.
         */
        [[nodiscard]] constexpr const char* to_text(SocketStatus status) noexcept
        {
            switch (status)
            {
                case SocketStatus::Ok:              return "ok";
                case SocketStatus::Timeout:         return "timed out";
                case SocketStatus::Closed:          return "the peer closed the connection";
                case SocketStatus::ResolveFailed:   return "the host has no address";
                case SocketStatus::InvalidArgument: return "invalid argument";
                case SocketStatus::Failed:          return "failed";
            }
            return "unknown status";
        }

        /**
         * @brief Carries the outcome of one socket call: status, OS error and its text.
         */
        struct SocketResult
        {
            /** @brief How the call ended. */
            SocketStatus status = SocketStatus::Ok;

            /** @brief OS error code, or 0; after ResolveFailed on POSIX, a getaddrinfo code. */
            int os_error = 0;

            /** @brief Bytes read_exact() or write_all() moved before it returned. */
            std::size_t transferred = 0;

            /** @brief Name of the OS call that ended the operation; a string literal. */
            const char* operation = "";

            /** @brief Text of os_error, or the seam's own reason when the OS gave none. */
            std::string cause;

            /** @brief Numeric `address:port` the failing call used, when one was known. */
            std::string address;

            /**
             * @brief Returns whether the call did all it was asked.
             */
            [[nodiscard]] bool is_ok() const noexcept
            {
                return status == SocketStatus::Ok;
            }
        };

        /**
         * @brief Returns the OS text of an error code, in English where the OS has it.
         * @param os_error An errno value on POSIX, a WSAGetLastError() value on Windows.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::string describe_os_error(int os_error);

        /**
         * @brief Formats a result as `operation address: cause (os error N)` for a log line.
         */
        [[nodiscard]] inline std::string describe(const SocketResult& result)
        {
            std::string text(result.operation);
            if (!result.address.empty())
                text.append(text.empty() ? "" : " ").append(result.address);
            text.append(text.empty() ? "" : ": ").append(result.cause);
            if (result.os_error != 0)
                text.append(" (os error ").append(std::to_string(result.os_error)).append(")");
            return text;
        }

        /**
         * @brief Lists every address of a host as numeric text, in the order the OS returns.
         * @param addresses Receives the addresses; cleared first.
         * @return ResolveFailed with the resolver's text when the host has no address.
         */
        [[nodiscard]] SUSHIRUNTIME_API SocketResult
        resolve_addresses(const Endpoint& endpoint, std::vector<std::string>& addresses);

        /**
         * @brief Returns the name the OS gives this machine.
         * @return An empty string when the OS reports none.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::string host_name();

        /**
         * @brief Holds helpers the two platform sources share.
         */
        namespace Detail
        {
            /**
             * @brief Builds a failed result whose cause is given as text.
             * @param operation A string literal.
             */
            [[nodiscard]] inline SocketResult socket_failure(SocketStatus status,
                                                             const char* operation, int os_error,
                                                             std::string cause,
                                                             std::string address = std::string())
            {
                SocketResult result;
                result.status    = status;
                result.os_error  = os_error;
                result.operation = operation;
                result.cause     = std::move(cause);
                result.address   = std::move(address);
                return result;
            }

            /**
             * @brief Builds a failed result whose cause is the OS text of @p os_error.
             * @param os_error 0 when the OS reported none; the cause is then the status words.
             */
            [[nodiscard]] inline SocketResult os_failure(SocketStatus status, const char* operation,
                                                         int os_error,
                                                         std::string address = std::string())
            {
                return socket_failure(status, operation, os_error,
                                      os_error != 0 ? describe_os_error(os_error) : to_text(status),
                                      std::move(address));
            }
        } // namespace Detail

        /**
         * @brief Owns one connected TCP socket and moves whole byte ranges through it.
         *
         * No member throws on an I/O failure. shutdown() may run on another thread while
         * read_exact() or write_all() waits; every other pair of calls needs the caller's
         * own ordering.
         */
        class SUSHIRUNTIME_API Socket final
        {
            public:
                /**
                 * @brief Creates a socket that is not open.
                 */
                Socket() noexcept = default;

                /**
                 * @brief Adopts an open, connected, non-blocking OS socket.
                 * @param handle Ownership passes to the object, which closes it.
                 */
                explicit Socket(NativeSocketHandle handle) noexcept : handle_(handle)
                {

                }

                /**
                 * @brief Closes the socket.
                 */
                ~Socket()
                {
                    close();
                }

                Socket(const Socket&)            = delete;
                Socket& operator=(const Socket&) = delete;

                /**
                 * @brief Takes the socket of @p other and leaves it not open.
                 */
                Socket(Socket&& other) noexcept
                    : handle_(std::exchange(other.handle_, K_INVALID_SOCKET_HANDLE)),
                      write_closed_(other.write_closed_.load()),
                      shut_down_(other.shut_down_.load())
                {

                }

                /**
                 * @brief Closes this socket, then takes the socket of @p other.
                 */
                Socket& operator=(Socket&& other) noexcept
                {
                    if (this != &other)
                    {
                        close();
                        handle_ = std::exchange(other.handle_, K_INVALID_SOCKET_HANDLE);
                        write_closed_.store(other.write_closed_.load());
                        shut_down_.store(other.shut_down_.load());
                    }
                    return *this;
                }

                /**
                 * @brief Resolves @p remote and connects to the first address that accepts.
                 * @param timeout Limit for each address, not for the whole call.
                 * @pre is_open() is false.
                 * @return The failure of the last address tried when none accepts.
                 */
                [[nodiscard]] SocketResult connect(const Endpoint& remote,
                                                   std::chrono::milliseconds timeout);

                /**
                 * @brief Reads until @p destination is full.
                 * @param idle_timeout Longest time with no byte arriving before Timeout.
                 * @return Closed when the peer ends the stream first; transferred counts the
                 * bytes read either way.
                 */
                [[nodiscard]] SocketResult read_exact(SushiRuntime::span<std::byte> destination,
                                                      std::chrono::milliseconds idle_timeout);

                /**
                 * @brief Writes every byte of @p source and raises no signal when the peer is gone.
                 * @param idle_timeout Longest time with no byte leaving before Timeout.
                 * @return Closed, with no OS call, on every call after one that did not return Ok.
                 */
                [[nodiscard]] SocketResult write_all(SushiRuntime::span<const std::byte> source,
                                                     std::chrono::milliseconds idle_timeout);

                /**
                 * @brief Reads the numeric address and the port of the peer.
                 */
                [[nodiscard]] SocketResult peer_endpoint(Endpoint& peer) const;

                /**
                 * @brief Ends both directions; a read or a write waiting on another thread returns
                 *        Closed within 100 ms, whether or not the peer answers.
                 */
                void shutdown() noexcept;

                /**
                 * @brief Releases the OS socket; does nothing when it is not open.
                 * @pre No other thread is inside a member of this object.
                 */
                void close() noexcept;

                /**
                 * @brief Returns whether the object holds an OS socket.
                 */
                [[nodiscard]] bool is_open() const noexcept
                {
                    return handle_ != K_INVALID_SOCKET_HANDLE;
                }

            private:
                /** @brief The OS socket, or K_INVALID_SOCKET_HANDLE. */
                NativeSocketHandle handle_ = K_INVALID_SOCKET_HANDLE;

                /** @brief Set by the first write_all() that does not return Ok. */
                std::atomic<bool> write_closed_{false};

                /** @brief Set by shutdown(); a waiting read or write reads it between waits. */
                std::atomic<bool> shut_down_{false};
        };

        /**
         * @brief Owns one listening TCP socket and hands out the connections it accepts.
         */
        class SUSHIRUNTIME_API Listener final
        {
            public:
                /**
                 * @brief Creates a listener that is not open.
                 */
                Listener() noexcept = default;

                /**
                 * @brief Closes the listener.
                 */
                ~Listener()
                {
                    close();
                }

                Listener(const Listener&)            = delete;
                Listener& operator=(const Listener&) = delete;

                /**
                 * @brief Takes the listener of @p other and leaves it not open.
                 */
                Listener(Listener&& other) noexcept
                    : handle_(std::exchange(other.handle_, K_INVALID_SOCKET_HANDLE)),
                      local_(std::move(other.local_))
                {

                }

                /**
                 * @brief Closes this listener, then takes the listener of @p other.
                 */
                Listener& operator=(Listener&& other) noexcept
                {
                    if (this != &other)
                    {
                        close();
                        handle_ = std::exchange(other.handle_, K_INVALID_SOCKET_HANDLE);
                        local_  = std::move(other.local_);
                    }
                    return *this;
                }

                /**
                 * @brief Binds the first address of @p bind_address that the OS grants and listens.
                 * @param bind_address The host must be given; port 0 takes any free port.
                 * @param backlog Pending connections the OS may queue; values below 1 become 1.
                 * @pre is_open() is false.
                 * @return A failure whose address and cause name the port and why it was refused.
                 */
                [[nodiscard]] SocketResult listen(const Endpoint& bind_address, int backlog);

                /**
                 * @brief Waits for one connection and returns it non-blocking.
                 * @param accepted Receives the connection; a socket it held is closed.
                 * @param timeout Longest wait before Timeout, so the caller can poll a stop flag.
                 */
                [[nodiscard]] SocketResult accept(Socket& accepted,
                                                  std::chrono::milliseconds timeout);

                /**
                 * @brief Releases the OS socket; does nothing when it is not open.
                 * @pre No other thread is inside accept().
                 */
                void close() noexcept;

                /**
                 * @brief Returns whether the object holds an OS socket.
                 */
                [[nodiscard]] bool is_open() const noexcept
                {
                    return handle_ != K_INVALID_SOCKET_HANDLE;
                }

                /**
                 * @brief Returns the numeric address and the port the OS bound.
                 * @pre listen() returned Ok.
                 */
                [[nodiscard]] const Endpoint& local_endpoint() const noexcept
                {
                    return local_;
                }

            private:
                /** @brief The OS socket, or K_INVALID_SOCKET_HANDLE. */
                NativeSocketHandle handle_ = K_INVALID_SOCKET_HANDLE;

                /** @brief Address and port read back from the OS after listen(). */
                Endpoint local_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
