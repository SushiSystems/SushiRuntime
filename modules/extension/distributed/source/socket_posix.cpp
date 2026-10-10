/**************************************************************************/
/* socket_posix.cpp                                                       */
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
 * @file socket_posix.cpp
 * @brief Implements Socket and Listener on POSIX sockets.
 * @author Mustafa Garip
 */

#include "socket.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>

#if !defined(_WIN32)

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
    #error "Neither MSG_NOSIGNAL nor SO_NOSIGPIPE exists here; a write could raise SIGPIPE."
#endif

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
#if defined(MSG_NOSIGNAL)
            /** @brief Flags of every send(): the call returns EPIPE and raises no SIGPIPE. */
            constexpr int K_SEND_FLAGS = MSG_NOSIGNAL;
#else
            /** @brief Flags of every send(); SO_NOSIGPIPE on the socket suppresses the signal. */
            constexpr int K_SEND_FLAGS = 0;
#endif

            /** @brief Value of a descriptor that is not open. */
            constexpr int K_NO_DESCRIPTOR = -1;

            /** @brief Room for a numeric address with an IPv6 scope name, and its end. */
            constexpr std::size_t K_HOST_TEXT_SIZE = 128;

            /** @brief Room for a decimal port and its end. */
            constexpr std::size_t K_PORT_TEXT_SIZE = 8;

            /** @brief Room for a machine name of 255 bytes and its end. */
            constexpr std::size_t K_HOST_NAME_SIZE = 256;

            /** @brief Direction wait_ready() waits for. */
            enum class Readiness : std::uint8_t
            {
                Readable, /**< Bytes or a connection can be taken. */
                Writable, /**< Bytes can be given, or a connect has ended. */
            };

            /**
             * @brief Converts the seam's handle to a descriptor.
             */
            int to_native(NativeSocketHandle handle) noexcept
            {
                return static_cast<int>(handle);
            }

            /**
             * @brief Picks the text of the XSI strerror_r, which returns a status.
             */
            [[maybe_unused]] const char* error_text(int status, const char* buffer) noexcept
            {
                return status == 0 ? buffer : "unknown error";
            }

            /**
             * @brief Picks the text of the GNU strerror_r, which returns the text itself.
             */
            [[maybe_unused]] const char* error_text(const char* text, const char*) noexcept
            {
                return text != nullptr ? text : "unknown error";
            }

            /**
             * @brief Owns the address list getaddrinfo returns for one endpoint.
             */
            class AddressList final
            {
                public:
                    /**
                     * @brief Resolves the host for TCP over IPv4 and IPv6.
                     */
                    explicit AddressList(const Endpoint& endpoint) noexcept
                    {
                        addrinfo hints{};
                        hints.ai_family   = AF_UNSPEC;
                        hints.ai_socktype = SOCK_STREAM;
                        hints.ai_protocol = IPPROTO_TCP;
                        hints.ai_flags    = AI_NUMERICSERV;

                        const std::string port = std::to_string(endpoint.port);
                        error_ = ::getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints,
                                               &first_);
                        system_error_ = errno;
                    }

                    /**
                     * @brief Frees the list.
                     */
                    ~AddressList()
                    {
                        if (first_ != nullptr)
                            ::freeaddrinfo(first_);
                    }

                    AddressList(const AddressList&)            = delete;
                    AddressList& operator=(const AddressList&) = delete;
                    AddressList(AddressList&&)                 = delete;
                    AddressList& operator=(AddressList&&)      = delete;

                    /**
                     * @brief Returns the getaddrinfo code of the lookup, or 0.
                     */
                    [[nodiscard]] int error() const noexcept
                    {
                        return error_;
                    }

                    /**
                     * @brief Returns the resolver's text for the failed lookup.
                     */
                    [[nodiscard]] std::string error_message() const
                    {
                        if (error_ == EAI_SYSTEM)
                            return describe_os_error(system_error_);
                        return ::gai_strerror(error_);
                    }

                    /**
                     * @brief Returns the first address, or nullptr when the lookup failed.
                     */
                    [[nodiscard]] const addrinfo* first() const noexcept
                    {
                        return first_;
                    }

                private:
                    /** @brief Head of the list getaddrinfo allocated. */
                    addrinfo* first_ = nullptr;

                    /** @brief Return value of getaddrinfo, an EAI code. */
                    int error_ = 0;

                    /** @brief errno read after getaddrinfo; meaningful for EAI_SYSTEM. */
                    int system_error_ = 0;
            };

            /**
             * @brief Builds the result of a lookup that returned no address.
             */
            SocketResult resolve_failure(const AddressList& addresses, const Endpoint& endpoint)
            {
                return Detail::socket_failure(SocketStatus::ResolveFailed, "getaddrinfo",
                                              addresses.error(), addresses.error_message(),
                                              endpoint.to_string());
            }

            /**
             * @brief Reads a socket address as numeric host text and port.
             * @return Whether the OS could format the address.
             */
            bool to_endpoint(const sockaddr* address, socklen_t length, Endpoint& endpoint)
            {
                char host[K_HOST_TEXT_SIZE];
                char port[K_PORT_TEXT_SIZE];
                if (::getnameinfo(address, length, host, sizeof(host), port, sizeof(port),
                                  NI_NUMERICHOST | NI_NUMERICSERV) != 0)
                    return false;

                endpoint.host = host;
                endpoint.port = static_cast<std::uint16_t>(std::strtoul(port, nullptr, 10));
                return true;
            }

            /**
             * @brief Formats a socket address as `address:port`, or as nothing when it has no form.
             */
            std::string to_address_text(const sockaddr* address, socklen_t length)
            {
                Endpoint endpoint;
                if (!to_endpoint(address, length, endpoint))
                    return std::string();
                return endpoint.to_string();
            }

            /**
             * @brief Sets one integer socket option.
             * @return Whether the OS took it.
             */
            bool set_option(int descriptor, int level, int name, int value) noexcept
            {
                return ::setsockopt(descriptor, level, name, &value, sizeof(value)) == 0;
            }

            /**
             * @brief Makes a descriptor non-blocking, closed on exec and silent on a broken pipe.
             * @return nullptr, or the name of the call that failed; errno holds its cause.
             */
            const char* configure(int descriptor) noexcept
            {
                const int status_flags = ::fcntl(descriptor, F_GETFL, 0);
                if (status_flags < 0 || ::fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) < 0)
                    return "fcntl(O_NONBLOCK)";

                const int descriptor_flags = ::fcntl(descriptor, F_GETFD, 0);
                if (descriptor_flags < 0 ||
                    ::fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0)
                    return "fcntl(FD_CLOEXEC)";

#if defined(SO_NOSIGPIPE)
                if (!set_option(descriptor, SOL_SOCKET, SO_NOSIGPIPE, 1))
                    return "setsockopt(SO_NOSIGPIPE)";
#endif
                return nullptr;
            }

            /**
             * @brief Returns whether an error means "try again once the socket is ready".
             */
            bool is_would_block(int error) noexcept
            {
                return error == EAGAIN || error == EWOULDBLOCK;
            }

            /**
             * @brief Returns whether an error means the connection is gone for good.
             */
            bool is_peer_gone(int error) noexcept
            {
                return error == EPIPE || error == ECONNRESET || error == ECONNABORTED ||
                       error == ENOTCONN;
            }

            /**
             * @brief Waits until a descriptor is ready in one direction, or has failed.
             * @param timeout Negative waits without a limit.
             * @param os_error Receives errno when the status is Failed.
             * @return Ok, Timeout or Failed; a signal does not shorten or end the wait.
             */
            SocketStatus wait_ready(int descriptor, Readiness readiness,
                                    std::chrono::milliseconds timeout, int& os_error) noexcept
            {
                using Clock = std::chrono::steady_clock;
                const bool              is_forever = timeout.count() < 0;
                const Clock::time_point deadline   = Clock::now() + (is_forever ? timeout.zero()
                                                                                : timeout);
                pollfd watched{};
                watched.fd     = descriptor;
                watched.events = readiness == Readiness::Readable ? POLLIN : POLLOUT;

                for (;;)
                {
                    int wait = -1;
                    if (!is_forever)
                    {
                        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - Clock::now());
                        wait = static_cast<int>(
                            std::clamp<std::chrono::milliseconds::rep>(left.count(), 0, INT_MAX));
                    }

                    const int ready = ::poll(&watched, 1, wait);
                    if (ready > 0)
                        return SocketStatus::Ok;
                    if (ready == 0)
                        return SocketStatus::Timeout;
                    if (errno != EINTR)
                    {
                        os_error = errno;
                        return SocketStatus::Failed;
                    }
                }
            }

            /**
             * @brief Reads the error a non-blocking connect ended with.
             * @return 0 when the connection stands.
             */
            int pending_error(int descriptor) noexcept
            {
                int       error  = 0;
                socklen_t length = sizeof(error);
                if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &length) != 0)
                    return errno;
                return error;
            }

            /**
             * @brief Connects a fresh socket to one address.
             * @param connected Receives the descriptor when the status is Ok; closed otherwise.
             */
            SocketResult connect_one(const addrinfo& address, std::chrono::milliseconds timeout,
                                     int& connected)
            {
                std::string text = to_address_text(address.ai_addr, address.ai_addrlen);

                const int descriptor = ::socket(address.ai_family, address.ai_socktype,
                                                address.ai_protocol);
                if (descriptor < 0)
                    return Detail::os_failure(SocketStatus::Failed, "socket", errno,
                                              std::move(text));

                SocketStatus status    = SocketStatus::Ok;
                const char*  operation = configure(descriptor);
                int          error     = 0;

                if (operation != nullptr)
                {
                    status = SocketStatus::Failed;
                    error  = errno;
                }
                else if (::connect(descriptor, address.ai_addr, address.ai_addrlen) != 0)
                {
                    error = errno;
                    // After EINTR the connect goes on in the kernel, as after EINPROGRESS.
                    if (error == EINPROGRESS || error == EINTR)
                    {
                        error  = 0;
                        status = wait_ready(descriptor, Readiness::Writable, timeout, error);
                        if (status == SocketStatus::Ok)
                            error = pending_error(descriptor);
                    }
                    if (status == SocketStatus::Ok && error != 0)
                        status = SocketStatus::Failed;
                }

                if (status != SocketStatus::Ok)
                {
                    (void)::close(descriptor);
                    return Detail::os_failure(status, operation != nullptr ? operation : "connect",
                                              error, std::move(text));
                }

                // A frame is written whole, so Nagle's delay only adds latency.
                (void)set_option(descriptor, IPPROTO_TCP, TCP_NODELAY, 1);
                connected = descriptor;
                return SocketResult();
            }

            /**
             * @brief Prepares, binds and listens on a fresh socket.
             * @param local Receives the address and port the OS bound.
             * @return nullptr, or the name of the call that failed; the OS error is still set.
             */
            const char* open_listener(int descriptor, const addrinfo& address, int backlog,
                                      Endpoint& local)
            {
                // An IPv6 listener takes IPv6 only, as on Windows, so "::" means one family.
                if (!set_option(descriptor, SOL_SOCKET, SO_REUSEADDR, 1))
                    return "setsockopt(SO_REUSEADDR)";
                if (address.ai_family == AF_INET6 &&
                    !set_option(descriptor, IPPROTO_IPV6, IPV6_V6ONLY, 1))
                    return "setsockopt(IPV6_V6ONLY)";
                const char* const failed_setup = configure(descriptor);
                if (failed_setup != nullptr)
                    return failed_setup;
                if (::bind(descriptor, address.ai_addr, address.ai_addrlen) != 0)
                    return "bind";
                if (::listen(descriptor, backlog) != 0)
                    return "listen";

                sockaddr_storage bound{};
                socklen_t length = sizeof(bound);
                if (::getsockname(descriptor, reinterpret_cast<sockaddr*>(&bound), &length) != 0)
                    return "getsockname";
                if (!to_endpoint(reinterpret_cast<const sockaddr*>(&bound), length, local))
                    return "getnameinfo";
                return nullptr;
            }

            /**
             * @brief Binds a fresh socket to one address and listens on it.
             * @param listening Receives the socket when the status is Ok.
             * @param local Receives the address and port the OS bound.
             */
            SocketResult listen_one(const addrinfo& address, int backlog, int& listening,
                                    Endpoint& local)
            {
                std::string text = to_address_text(address.ai_addr, address.ai_addrlen);

                const int descriptor = ::socket(address.ai_family, address.ai_socktype,
                                               address.ai_protocol);
                if (descriptor < 0)
                    return Detail::os_failure(SocketStatus::Failed, "socket", errno,
                                              std::move(text));

                const char* const failed_call = open_listener(descriptor, address, backlog,
                                                              local);
                if (failed_call != nullptr)
                {
                    const int error = errno;
                    (void)::close(descriptor);
                    return Detail::os_failure(SocketStatus::Failed, failed_call, error,
                                              std::move(text));
                }

                listening = descriptor;
                return SocketResult();
            }
        } // namespace

        /**
         * @brief Returns the strerror text of an errno value.
         */
        std::string describe_os_error(int os_error)
        {
            char buffer[256] = "";
            return error_text(::strerror_r(os_error, buffer, sizeof(buffer)), buffer);
        }

        /**
         * @brief Lists every address of a host as numeric text, in the order the OS returns.
         */
        SocketResult resolve_addresses(const Endpoint& endpoint,
                                       std::vector<std::string>& addresses)
        {
            addresses.clear();

            const AddressList list(endpoint);
            if (list.first() == nullptr)
                return resolve_failure(list, endpoint);

            for (const addrinfo* entry = list.first(); entry != nullptr; entry = entry->ai_next)
            {
                Endpoint numeric;
                if (to_endpoint(entry->ai_addr, entry->ai_addrlen, numeric))
                    addresses.push_back(std::move(numeric.host));
            }
            return SocketResult();
        }

        /**
         * @brief Returns the name the kernel gives this machine, or an empty string.
         */
        std::string host_name()
        {
            char buffer[K_HOST_NAME_SIZE] = "";
            if (::gethostname(buffer, sizeof(buffer)) != 0)
                return std::string();

            buffer[sizeof(buffer) - 1] = '\0';
            return std::string(buffer);
        }

        /**
         * @brief Resolves the endpoint and connects to the first address that accepts.
         */
        SocketResult Socket::connect(const Endpoint& remote, std::chrono::milliseconds timeout)
        {
            if (is_open())
                return Detail::socket_failure(SocketStatus::InvalidArgument, "connect", 0,
                                              "the socket is already open", remote.to_string());

            const AddressList list(remote);
            if (list.first() == nullptr)
                return resolve_failure(list, remote);

            SocketResult result;
            for (const addrinfo* entry = list.first(); entry != nullptr; entry = entry->ai_next)
            {
                int connected = K_NO_DESCRIPTOR;
                result = connect_one(*entry, timeout, connected);
                if (result.is_ok())
                {
                    handle_ = connected;
                    write_closed_.store(false);
                    break;
                }
            }
            return result;
        }

        /**
         * @brief Reads until the destination is full, waiting while no byte is ready.
         */
        SocketResult Socket::read_exact(SushiRuntime::span<std::byte> destination,
                                        std::chrono::milliseconds idle_timeout)
        {
            const int    descriptor = to_native(handle_);
            char* const  bytes      = reinterpret_cast<char*>(destination.data());
            SocketResult result;

            while (result.is_ok() && result.transferred < destination.size())
            {
                const std::size_t done     = result.transferred;
                const ssize_t     received = ::recv(descriptor, bytes + done,
                                                    destination.size() - done, 0);
                if (received > 0)
                {
                    result.transferred += static_cast<std::size_t>(received);
                    continue;
                }

                SocketStatus status = SocketStatus::Closed;
                int          error  = received == 0 ? 0 : errno;
                if (error == EINTR)
                    continue;
                if (is_would_block(error))
                {
                    error  = 0;
                    status = wait_ready(descriptor, Readiness::Readable, idle_timeout, error);
                    if (status == SocketStatus::Ok)
                        continue;
                }
                else if (error != 0 && !is_peer_gone(error))
                {
                    status = SocketStatus::Failed;
                }

                result             = Detail::os_failure(status, "recv", error);
                result.transferred = done;
            }
            return result;
        }

        /**
         * @brief Writes every byte, waiting while the OS buffer is full; raises no SIGPIPE.
         */
        SocketResult Socket::write_all(SushiRuntime::span<const std::byte> source,
                                       std::chrono::milliseconds idle_timeout)
        {
            if (write_closed_.load())
                return Detail::socket_failure(SocketStatus::Closed, "send", 0,
                                              "an earlier write on this socket failed");

            const int         descriptor = to_native(handle_);
            const char* const bytes      = reinterpret_cast<const char*>(source.data());
            SocketResult      result;

            while (result.is_ok() && result.transferred < source.size())
            {
                const std::size_t done = result.transferred;
                const ssize_t     sent = ::send(descriptor, bytes + done, source.size() - done,
                                                K_SEND_FLAGS);
                if (sent > 0)
                {
                    result.transferred += static_cast<std::size_t>(sent);
                    continue;
                }

                SocketStatus status = SocketStatus::Closed;
                int          error  = sent == 0 ? 0 : errno;
                if (error == EINTR)
                    continue;
                if (is_would_block(error))
                {
                    error  = 0;
                    status = wait_ready(descriptor, Readiness::Writable, idle_timeout, error);
                    if (status == SocketStatus::Ok)
                        continue;
                }
                else if (error != 0 && !is_peer_gone(error))
                {
                    status = SocketStatus::Failed;
                }

                result             = Detail::os_failure(status, "send", error);
                result.transferred = done;
                write_closed_.store(true);
            }
            return result;
        }

        /**
         * @brief Reads the numeric address and the port of the peer.
         */
        SocketResult Socket::peer_endpoint(Endpoint& peer) const
        {
            sockaddr_storage address{};
            socklen_t        length = sizeof(address);
            if (::getpeername(to_native(handle_), reinterpret_cast<sockaddr*>(&address),
                              &length) != 0)
                return Detail::os_failure(SocketStatus::Failed, "getpeername", errno);
            if (!to_endpoint(reinterpret_cast<const sockaddr*>(&address), length, peer))
                return Detail::os_failure(SocketStatus::Failed, "getnameinfo", errno);
            return SocketResult();
        }

        /**
         * @brief Ends both directions of the connection.
         */
        void Socket::shutdown() noexcept
        {
            if (is_open())
                (void)::shutdown(to_native(handle_), SHUT_RDWR);
        }

        /**
         * @brief Releases the descriptor.
         */
        void Socket::close() noexcept
        {
            if (is_open())
                (void)::close(to_native(handle_));
            handle_ = K_INVALID_SOCKET_HANDLE;
        }

        /**
         * @brief Binds the first address the OS grants and listens.
         */
        SocketResult Listener::listen(const Endpoint& bind_address, int backlog)
        {
            if (is_open() || bind_address.host.empty())
                return Detail::socket_failure(SocketStatus::InvalidArgument, "bind", 0,
                                              is_open() ? "the listener is already open"
                                                        : "the bind address has no host",
                                              bind_address.to_string());

            const AddressList list(bind_address);
            if (list.first() == nullptr)
                return resolve_failure(list, bind_address);

            SocketResult result;
            for (const addrinfo* entry = list.first(); entry != nullptr; entry = entry->ai_next)
            {
                int listening = K_NO_DESCRIPTOR;
                result = listen_one(*entry, std::max(backlog, 1), listening, local_);
                if (result.is_ok())
                {
                    handle_ = listening;
                    break;
                }
            }
            return result;
        }

        /**
         * @brief Waits for one connection and returns it non-blocking with Nagle off.
         */
        SocketResult Listener::accept(Socket& accepted, std::chrono::milliseconds timeout)
        {
            const int listening = to_native(handle_);
            for (;;)
            {
                int                error  = 0;
                const SocketStatus status = wait_ready(listening, Readiness::Readable, timeout,
                                                       error);
                if (status != SocketStatus::Ok)
                    return Detail::os_failure(status, "accept", error, local_.to_string());

                const int descriptor = ::accept(listening, nullptr, nullptr);
                if (descriptor < 0)
                {
                    error = errno;
                    // The client left between the wait and the call; the queue is still served.
                    if (is_would_block(error) || error == ECONNABORTED || error == EINTR)
                        continue;
                    return Detail::os_failure(SocketStatus::Failed, "accept", error,
                                              local_.to_string());
                }

                const char* const failed_call = configure(descriptor);
                if (failed_call != nullptr)
                {
                    error = errno;
                    (void)::close(descriptor);
                    return Detail::os_failure(SocketStatus::Failed, failed_call, error,
                                              local_.to_string());
                }

                (void)set_option(descriptor, IPPROTO_TCP, TCP_NODELAY, 1);
                accepted = Socket(static_cast<NativeSocketHandle>(descriptor));
                return SocketResult();
            }
        }

        /**
         * @brief Releases the descriptor.
         */
        void Listener::close() noexcept
        {
            if (is_open())
                (void)::close(to_native(handle_));
            handle_ = K_INVALID_SOCKET_HANDLE;
        }

    } // namespace Distributed
} // namespace SushiRuntime

#endif // !defined(_WIN32)
