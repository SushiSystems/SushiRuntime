/**************************************************************************/
/* socket_windows.cpp                                                     */
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
 * @file socket_windows.cpp
 * @brief Implements Socket and Listener on Winsock.
 * @author Mustafa Garip
 */

#include <SushiRuntime/network_socket/socket.hpp>
#include <SushiRuntime/network_socket/socket_option_report.hpp>

#include <algorithm>
#include <climits>
#include <cstdlib>

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
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
             * @brief Starts Winsock 2.2 once for the process.
             * @return 0, or the error WSAStartup returned on the first call.
             */
            int startup_error() noexcept
            {
                static const int error = []() noexcept
                {
                    WSADATA data;
                    return ::WSAStartup(MAKEWORD(2, 2), &data);
                }();
                return error;
            }

            /**
             * @brief Converts the seam's handle to the Winsock type.
             */
            SOCKET to_native(NativeSocketHandle handle) noexcept
            {
                return static_cast<SOCKET>(handle);
            }

            /**
             * @brief Converts a Winsock socket to the seam's handle; INVALID_SOCKET becomes -1.
             */
            NativeSocketHandle from_native(SOCKET socket) noexcept
            {
                return static_cast<NativeSocketHandle>(socket);
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
                     * @brief Returns the Winsock error of the lookup, or 0.
                     */
                    [[nodiscard]] int error() const noexcept
                    {
                        return error_;
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

                    /** @brief Return value of getaddrinfo. */
                    int error_ = 0;
            };

            /**
             * @brief Builds the result of a lookup that returned no address.
             */
            SocketResult resolve_failure(const AddressList& addresses, const Endpoint& endpoint)
            {
                return Detail::os_failure(SocketStatus::ResolveFailed, "getaddrinfo",
                                          addresses.error(), endpoint.to_string());
            }

            /**
             * @brief Reads a socket address as numeric host text and port.
             * @return Whether the OS could format the address.
             */
            bool to_endpoint(const sockaddr* address, int length, Endpoint& endpoint)
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
            std::string to_address_text(const sockaddr* address, int length)
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
            bool set_option(SOCKET socket, int level, int name, int value) noexcept
            {
                return ::setsockopt(socket, level, name, reinterpret_cast<const char*>(&value),
                                    sizeof(value)) == 0;
            }

            /**
             * @brief Puts a socket in non-blocking mode.
             */
            bool set_non_blocking(SOCKET socket) noexcept
            {
                u_long enabled = 1;
                return ::ioctlsocket(socket, FIONBIO, &enabled) == 0;
            }

            /**
             * @brief Returns whether an error means the connection is gone for good.
             */
            bool is_peer_gone(int error) noexcept
            {
                return error == WSAECONNRESET || error == WSAECONNABORTED ||
                       error == WSAESHUTDOWN || error == WSAENOTCONN || error == WSAENETRESET;
            }

            /**
             * @brief Waits until a socket is ready in one direction, or has failed.
             * @param timeout Negative waits without a limit.
             * @param os_error Receives the Winsock error when the status is Failed.
             * @return Ok, Timeout or Failed.
             */
            SocketStatus wait_ready(SOCKET socket, Readiness readiness,
                                    std::chrono::milliseconds timeout, int& os_error) noexcept
            {
                fd_set watched;
                fd_set failed;
                FD_ZERO(&watched);
                FD_ZERO(&failed);
                FD_SET(socket, &watched);
                FD_SET(socket, &failed);

                constexpr std::chrono::milliseconds::rep K_LONGEST_WAIT = 86400000;
                const std::chrono::milliseconds::rep wait =
                    std::min(timeout.count(), K_LONGEST_WAIT);

                timeval limit;
                limit.tv_sec  = static_cast<long>(wait / 1000);
                limit.tv_usec = static_cast<long>((wait % 1000) * 1000);

                const bool is_read = readiness == Readiness::Readable;
                const int  ready   = ::select(0, is_read ? &watched : nullptr,
                                              is_read ? nullptr : &watched, &failed,
                                              timeout.count() < 0 ? nullptr : &limit);
                if (ready == SOCKET_ERROR)
                {
                    os_error = ::WSAGetLastError();
                    return SocketStatus::Failed;
                }
                return ready == 0 ? SocketStatus::Timeout : SocketStatus::Ok;
            }

            /** @brief Longest single wait of a read or a write before it reads the shut flag. */
            constexpr std::chrono::milliseconds K_SHUT_POLL{100};

            /**
             * @brief Waits like wait_ready(), and ends with Closed once @p is_shut is set.
             *
             * select() on Windows does not wake when another thread shuts the socket and the
             * peer never answers, so the wait runs in slices of K_SHUT_POLL.
             * @return Ok, Timeout, Closed or Failed.
             */
            SocketStatus wait_ready_or_shut(SOCKET socket, Readiness readiness,
                                            std::chrono::milliseconds timeout,
                                            const std::atomic<bool>& is_shut,
                                            int& os_error) noexcept
            {
                using Clock = std::chrono::steady_clock;

                const bool              is_bounded = timeout.count() >= 0;
                const Clock::time_point deadline =
                    Clock::now() + (is_bounded ? timeout : K_SHUT_POLL);
                for (;;)
                {
                    if (is_shut.load(std::memory_order_acquire))
                        return SocketStatus::Closed;

                    std::chrono::milliseconds slice = K_SHUT_POLL;
                    if (is_bounded)
                    {
                        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - Clock::now());
                        if (left.count() <= 0)
                            return SocketStatus::Timeout;
                        slice = std::min(slice, left);
                    }

                    const SocketStatus status = wait_ready(socket, readiness, slice, os_error);
                    if (status != SocketStatus::Timeout)
                        return status;
                }
            }

            /**
             * @brief Reads the error a non-blocking connect ended with.
             * @return 0 when the connection stands.
             */
            int pending_error(SOCKET socket) noexcept
            {
                int error  = 0;
                int length = sizeof(error);
                if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error),
                                 &length) != 0)
                    return ::WSAGetLastError();
                return error;
            }

            /**
             * @brief Connects a fresh socket to one address.
             * @param connected Receives the socket when the status is Ok; closed otherwise.
             */
            SocketResult connect_one(const addrinfo& address, std::chrono::milliseconds timeout,
                                     SOCKET& connected)
            {
                const int   length = static_cast<int>(address.ai_addrlen);
                std::string text   = to_address_text(address.ai_addr, length);

                const SOCKET socket = ::socket(address.ai_family, address.ai_socktype,
                                               address.ai_protocol);
                if (socket == INVALID_SOCKET)
                    return Detail::os_failure(SocketStatus::Failed, "socket", ::WSAGetLastError(),
                                              std::move(text));

                SocketStatus status    = SocketStatus::Ok;
                const char*  operation = "connect";
                int          error     = 0;

                if (!set_non_blocking(socket))
                {
                    status    = SocketStatus::Failed;
                    operation = "ioctlsocket";
                    error     = ::WSAGetLastError();
                }
                else if (::connect(socket, address.ai_addr, length) != 0)
                {
                    error = ::WSAGetLastError();
                    if (error == WSAEWOULDBLOCK)
                    {
                        error  = 0;
                        status = wait_ready(socket, Readiness::Writable, timeout, error);
                        if (status == SocketStatus::Ok)
                            error = pending_error(socket);
                    }
                    if (status == SocketStatus::Ok && error != 0)
                        status = SocketStatus::Failed;
                }

                if (status != SocketStatus::Ok)
                {
                    ::closesocket(socket);
                    return Detail::os_failure(status, operation, error, std::move(text));
                }

                // A frame is written whole, so Nagle's delay only adds latency.
                if (!set_option(socket, IPPROTO_TCP, TCP_NODELAY, 1))
                    Detail::report_no_delay_failure(text, ::WSAGetLastError());
                connected = socket;
                return SocketResult();
            }

            /**
             * @brief Prepares, binds and listens on a fresh socket.
             * @param local Receives the address and port the OS bound.
             * @return nullptr, or the name of the call that failed; the OS error is still set.
             */
            const char* open_listener(SOCKET socket, const addrinfo& address, int backlog,
                                      Endpoint& local)
            {
                // An IPv6 listener takes IPv6 only, as on POSIX, so "::" means one family.
                if (!set_option(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1))
                    return "setsockopt(SO_EXCLUSIVEADDRUSE)";
                if (address.ai_family == AF_INET6 &&
                    !set_option(socket, IPPROTO_IPV6, IPV6_V6ONLY, 1))
                    return "setsockopt(IPV6_V6ONLY)";
                if (!set_non_blocking(socket))
                    return "ioctlsocket";
                if (::bind(socket, address.ai_addr, static_cast<int>(address.ai_addrlen)) != 0)
                    return "bind";
                if (::listen(socket, backlog) != 0)
                    return "listen";

                sockaddr_storage bound{};
                int length = sizeof(bound);
                if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length) != 0)
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
            SocketResult listen_one(const addrinfo& address, int backlog, SOCKET& listening,
                                    Endpoint& local)
            {
                const int   length = static_cast<int>(address.ai_addrlen);
                std::string text   = to_address_text(address.ai_addr, length);

                const SOCKET socket = ::socket(address.ai_family, address.ai_socktype,
                                               address.ai_protocol);
                if (socket == INVALID_SOCKET)
                    return Detail::os_failure(SocketStatus::Failed, "socket", ::WSAGetLastError(),
                                              std::move(text));

                const char* const failed_call = open_listener(socket, address, backlog, local);
                if (failed_call != nullptr)
                {
                    const int error = ::WSAGetLastError();
                    (void)::closesocket(socket);
                    return Detail::os_failure(SocketStatus::Failed, failed_call, error,
                                              std::move(text));
                }

                listening = socket;
                return SocketResult();
            }
        } // namespace

        /**
         * @brief Returns the system text of a Winsock error, in English where installed.
         */
        std::string describe_os_error(int os_error)
        {
            constexpr DWORD K_FLAGS   = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
            constexpr DWORD K_ENGLISH = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

            char        buffer[512];
            const DWORD code   = static_cast<DWORD>(os_error);
            DWORD       length = ::FormatMessageA(K_FLAGS, nullptr, code, K_ENGLISH, buffer,
                                                  sizeof(buffer), nullptr);
            if (length == 0)
                length = ::FormatMessageA(K_FLAGS, nullptr, code, 0, buffer, sizeof(buffer),
                                          nullptr);
            if (length == 0)
                return "unknown error";

            // FormatMessage ends its text with a full stop and a line break.
            while (length > 0 && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n' ||
                                  buffer[length - 1] == '.' || buffer[length - 1] == ' '))
                --length;
            return std::string(buffer, length);
        }

        /**
         * @brief Lists every address of a host as numeric text, in the order the OS returns.
         */
        SocketResult resolve_addresses(const Endpoint& endpoint,
                                       std::vector<std::string>& addresses)
        {
            addresses.clear();
            if (startup_error() != 0)
                return Detail::os_failure(SocketStatus::Failed, "WSAStartup", startup_error());

            const AddressList list(endpoint);
            if (list.first() == nullptr)
                return resolve_failure(list, endpoint);

            for (const addrinfo* entry = list.first(); entry != nullptr; entry = entry->ai_next)
            {
                Endpoint numeric;
                if (to_endpoint(entry->ai_addr, static_cast<int>(entry->ai_addrlen), numeric))
                    addresses.push_back(std::move(numeric.host));
            }
            return SocketResult();
        }

        /**
         * @brief Returns the name Winsock gives this machine, or an empty string.
         */
        std::string host_name()
        {
            if (startup_error() != 0)
                return std::string();

            char buffer[K_HOST_NAME_SIZE] = "";
            if (::gethostname(buffer, static_cast<int>(sizeof(buffer))) != 0)
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
            if (startup_error() != 0)
                return Detail::os_failure(SocketStatus::Failed, "WSAStartup", startup_error());

            const AddressList list(remote);
            if (list.first() == nullptr)
                return resolve_failure(list, remote);

            SocketResult result;
            for (const addrinfo* entry = list.first(); entry != nullptr; entry = entry->ai_next)
            {
                SOCKET connected = INVALID_SOCKET;
                result = connect_one(*entry, timeout, connected);
                if (result.is_ok())
                {
                    handle_ = from_native(connected);
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
            const SOCKET socket = to_native(handle_);
            char* const  bytes  = reinterpret_cast<char*>(destination.data());
            SocketResult result;

            while (result.is_ok() && result.transferred < destination.size())
            {
                const std::size_t done     = result.transferred;
                const std::size_t left     = destination.size() - done;
                const int         chunk    = static_cast<int>(std::min<std::size_t>(left, INT_MAX));
                const int         received = ::recv(socket, bytes + done, chunk, 0);
                if (received > 0)
                {
                    result.transferred += static_cast<std::size_t>(received);
                    continue;
                }

                SocketStatus status = SocketStatus::Closed;
                int          error  = received == 0 ? 0 : ::WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    error  = 0;
                    status = wait_ready_or_shut(socket, Readiness::Readable, idle_timeout,
                                                shut_down_, error);
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
         * @brief Writes every byte, waiting while the OS buffer is full.
         */
        SocketResult Socket::write_all(SushiRuntime::span<const std::byte> source,
                                       std::chrono::milliseconds idle_timeout)
        {
            if (write_closed_.load())
                return Detail::socket_failure(SocketStatus::Closed, "send", 0,
                                              "an earlier write on this socket failed");

            const SOCKET      socket = to_native(handle_);
            const char* const bytes  = reinterpret_cast<const char*>(source.data());
            SocketResult      result;

            while (result.is_ok() && result.transferred < source.size())
            {
                const std::size_t done  = result.transferred;
                const std::size_t left  = source.size() - done;
                const int         chunk = static_cast<int>(std::min<std::size_t>(left, INT_MAX));
                const int         sent  = ::send(socket, bytes + done, chunk, 0);
                if (sent > 0)
                {
                    result.transferred += static_cast<std::size_t>(sent);
                    continue;
                }

                SocketStatus status = SocketStatus::Closed;
                int          error  = sent == 0 ? 0 : ::WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    error  = 0;
                    status = wait_ready_or_shut(socket, Readiness::Writable, idle_timeout,
                                                shut_down_, error);
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
            int              length = sizeof(address);
            if (::getpeername(to_native(handle_), reinterpret_cast<sockaddr*>(&address),
                              &length) != 0)
                return Detail::os_failure(SocketStatus::Failed, "getpeername",
                                          ::WSAGetLastError());
            if (!to_endpoint(reinterpret_cast<const sockaddr*>(&address), length, peer))
                return Detail::os_failure(SocketStatus::Failed, "getnameinfo",
                                          ::WSAGetLastError());
            return SocketResult();
        }

        /**
         * @brief Ends both directions of the connection.
         */
        void Socket::shutdown() noexcept
        {
            shut_down_.store(true, std::memory_order_release);
            if (is_open())
                (void)::shutdown(to_native(handle_), SD_BOTH);
        }

        /**
         * @brief Releases the OS socket.
         */
        void Socket::close() noexcept
        {
            if (is_open())
                (void)::closesocket(to_native(handle_));
            handle_ = K_INVALID_SOCKET_HANDLE;
        }

        /**
         * @brief Binds the first address the OS grants, for this process alone, and listens.
         */
        SocketResult Listener::listen(const Endpoint& bind_address, int backlog)
        {
            if (is_open() || bind_address.host.empty())
                return Detail::socket_failure(SocketStatus::InvalidArgument, "bind", 0,
                                              is_open() ? "the listener is already open"
                                                        : "the bind address has no host",
                                              bind_address.to_string());
            if (startup_error() != 0)
                return Detail::os_failure(SocketStatus::Failed, "WSAStartup", startup_error());

            const AddressList list(bind_address);
            if (list.first() == nullptr)
                return resolve_failure(list, bind_address);

            SocketResult result;
            for (const addrinfo* entry = list.first(); entry != nullptr; entry = entry->ai_next)
            {
                SOCKET listening = INVALID_SOCKET;
                result = listen_one(*entry, std::max(backlog, 1), listening, local_);
                if (result.is_ok())
                {
                    handle_ = from_native(listening);
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
            const SOCKET listening = to_native(handle_);
            for (;;)
            {
                int                error  = 0;
                const SocketStatus status = wait_ready(listening, Readiness::Readable, timeout,
                                                       error);
                if (status != SocketStatus::Ok)
                    return Detail::os_failure(status, "accept", error, local_.to_string());

                const SOCKET socket = ::accept(listening, nullptr, nullptr);
                if (socket == INVALID_SOCKET)
                {
                    error = ::WSAGetLastError();
                    // The client left between the wait and the call; the queue is still served.
                    if (error == WSAEWOULDBLOCK || error == WSAECONNRESET)
                        continue;
                    return Detail::os_failure(SocketStatus::Failed, "accept", error,
                                              local_.to_string());
                }

                if (!set_non_blocking(socket))
                {
                    error = ::WSAGetLastError();
                    ::closesocket(socket);
                    return Detail::os_failure(SocketStatus::Failed, "ioctlsocket", error,
                                              local_.to_string());
                }

                if (!set_option(socket, IPPROTO_TCP, TCP_NODELAY, 1))
                    Detail::report_no_delay_failure("a client of " + local_.to_string(),
                                                    ::WSAGetLastError());
                accepted = Socket(from_native(socket));
                return SocketResult();
            }
        }

        /**
         * @brief Releases the OS socket.
         */
        void Listener::close() noexcept
        {
            if (is_open())
                (void)::closesocket(to_native(handle_));
            handle_ = K_INVALID_SOCKET_HANDLE;
        }

    } // namespace Distributed
} // namespace SushiRuntime

#endif // defined(_WIN32)
