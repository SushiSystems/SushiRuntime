/**************************************************************************/
/* tcp_transport.cpp                                                      */
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

#include <SushiRuntime/distributed/tcp_transport.hpp>

#include <mutex>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>
#include <SushiRuntime/core/logger.hpp>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    using socket_t = int;
    static constexpr socket_t INVALID_SOCK = -1;
#endif

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            constexpr int MASTER_PEER = -1;

            void platform_init()
            {
#if defined(_WIN32)
                static std::once_flag once;
                std::call_once(once, []
                {
                    WSADATA wsa;
                    WSAStartup(MAKEWORD(2, 2), &wsa);
                });
#endif
            }

            void close_sock(socket_t s)
            {
                if (s == INVALID_SOCK) return;
#if defined(_WIN32)
                ::closesocket(s);
#else
                ::close(s);
#endif
            }

            // close() alone does NOT wake a recv()/accept() blocked in another
            // thread on Linux — shutdown() does. Call this before joining readers.
            void shutdown_sock(socket_t s)
            {
                if (s == INVALID_SOCK) return;
#if defined(_WIN32)
                ::shutdown(s, SD_BOTH);
#else
                ::shutdown(s, SHUT_RDWR);
#endif
            }

            void set_nodelay(socket_t s)
            {
                int one = 1;
                ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                             reinterpret_cast<const char*>(&one), sizeof(one));
            }

            // Blocking read/write of an exact byte count; false on close/error.
            bool read_n(socket_t s, void* buf, size_t n)
            {
                auto* p = static_cast<char*>(buf);
                size_t got = 0;
                while (got < n)
                {
                    auto r = ::recv(s, p + got, static_cast<int>(n - got), 0);
                    if (r <= 0) return false;
                    got += static_cast<size_t>(r);
                }
                return true;
            }

            bool write_n(socket_t s, const void* buf, size_t n)
            {
                const auto* p = static_cast<const char*>(buf);
                size_t sent = 0;
                while (sent < n)
                {
                    auto r = ::send(s, p + sent, static_cast<int>(n - sent), 0);
                    if (r <= 0) return false;
                    sent += static_cast<size_t>(r);
                }
                return true;
            }
        } // namespace

        struct TcpTransport::Impl
        {
            bool        is_master = false;
            int         worker_count = 0;
            std::string host;
            uint16_t    port = 0;

            MessageHandler    handler;
            std::atomic<bool> running{false};

            socket_t              listen_sock = INVALID_SOCK;     // master
            std::vector<socket_t> peers;                          // master: workers; worker: [master]
            std::vector<std::unique_ptr<std::mutex>> send_mtx;    // one per peer socket
            std::thread              accept_thread;               // master
            std::vector<std::thread> readers;

            void reader_loop(socket_t sock, int peer)
            {
                for (;;)
                {
                    unsigned char hdr[5];
                    if (!read_n(sock, hdr, sizeof(hdr))) break;

                    uint32_t len;
                    std::memcpy(&len, hdr, 4);
                    MsgType type = static_cast<MsgType>(hdr[4]);

                    std::vector<std::byte> payload(len);
                    if (len && !read_n(sock, payload.data(), len)) break;

                    if (handler && running.load())
                        handler(peer, type, std::span<const std::byte>(payload.data(), payload.size()));
                }
            }

            void send_to(socket_t sock, std::mutex& m, MsgType type, std::span<const std::byte> payload)
            {
                if (sock == INVALID_SOCK) return;
                unsigned char hdr[5];
                uint32_t len = static_cast<uint32_t>(payload.size());
                std::memcpy(hdr, &len, 4);
                hdr[4] = static_cast<unsigned char>(type);

                std::lock_guard<std::mutex> lk(m);
                if (!write_n(sock, hdr, sizeof(hdr))) return;
                if (len) write_n(sock, payload.data(), len);
            }

            void accept_loop()
            {
                for (int i = 0; i < worker_count && running.load(); ++i)
                {
                    socket_t s = ::accept(listen_sock, nullptr, nullptr);
                    if (s == INVALID_SOCK) break;
                    set_nodelay(s);
                    peers[static_cast<size_t>(i)] = s;
                    readers.emplace_back([this, s, i] { reader_loop(s, i); });
                    SR_LOG_INFO("TCP master accepted worker {}.", i);
                }
            }
        };

        TcpTransport::TcpTransport(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

        TcpTransport::~TcpTransport() { stop(); }

        std::shared_ptr<TcpTransport> TcpTransport::make_master(uint16_t port, int worker_count)
        {
            platform_init();
            auto impl = std::make_shared<Impl>();
            impl->is_master    = true;
            impl->worker_count = worker_count;
            impl->peers.assign(static_cast<size_t>(worker_count), INVALID_SOCK);
            for (int i = 0; i < worker_count; ++i)
                impl->send_mtx.push_back(std::make_unique<std::mutex>());

            socket_t ls = ::socket(AF_INET, SOCK_STREAM, 0);
            if (ls == INVALID_SOCK)
                throw std::runtime_error("TcpTransport: socket() failed for master.");

            int one = 1;
            ::setsockopt(ls, SOL_SOCKET, SO_REUSEADDR,
                         reinterpret_cast<const char*>(&one), sizeof(one));

            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port        = htons(port);

            if (::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
            {
                close_sock(ls);
                throw std::runtime_error("TcpTransport: bind() failed for master.");
            }
            if (::listen(ls, worker_count > 0 ? worker_count : 1) != 0)
            {
                close_sock(ls);
                throw std::runtime_error("TcpTransport: listen() failed for master.");
            }

            // Resolve the actual bound port (when port 0 was requested).
            sockaddr_in bound{};
#if defined(_WIN32)
            int blen = sizeof(bound);
#else
            socklen_t blen = sizeof(bound);
#endif
            if (::getsockname(ls, reinterpret_cast<sockaddr*>(&bound), &blen) == 0)
                impl->port = ntohs(bound.sin_port);

            impl->listen_sock = ls;
            return std::shared_ptr<TcpTransport>(new TcpTransport(impl));
        }

        std::shared_ptr<TcpTransport> TcpTransport::make_worker(const std::string& host, uint16_t port)
        {
            platform_init();
            auto impl = std::make_shared<Impl>();
            impl->is_master = false;
            impl->host      = host;
            impl->port      = port;
            impl->peers.assign(1, INVALID_SOCK);
            impl->send_mtx.push_back(std::make_unique<std::mutex>());
            return std::shared_ptr<TcpTransport>(new TcpTransport(impl));
        }

        void TcpTransport::on_message(MessageHandler handler) { impl_->handler = std::move(handler); }

        void TcpTransport::start()
        {
            bool expected = false;
            if (!impl_->running.compare_exchange_strong(expected, true))
                return;

            if (impl_->is_master)
            {
                impl_->accept_thread = std::thread([this] { impl_->accept_loop(); });
            }
            else
            {
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port   = htons(impl_->port);
                if (::inet_pton(AF_INET, impl_->host.c_str(), &addr.sin_addr) != 1)
                    throw std::runtime_error("TcpTransport: bad master host '" + impl_->host + "'.");

                // Retry so the worker can be started before the master (the
                // natural lab order) — keep trying until connected or stop().
                constexpr int  MAX_ATTEMPTS = 300;   // ~60s at 200ms
                socket_t s = INVALID_SOCK;
                for (int attempt = 0; impl_->running.load() && attempt < MAX_ATTEMPTS; ++attempt)
                {
                    s = ::socket(AF_INET, SOCK_STREAM, 0);
                    if (s == INVALID_SOCK)
                        throw std::runtime_error("TcpTransport: socket() failed for worker.");

                    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
                        break; // connected

                    close_sock(s);
                    s = INVALID_SOCK;
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                if (s == INVALID_SOCK)
                    throw std::runtime_error("TcpTransport: connect() to master failed (master not reachable).");

                set_nodelay(s);
                impl_->peers[0] = s;
                impl_->readers.emplace_back([this, s] { impl_->reader_loop(s, MASTER_PEER); });
                SR_LOG_INFO("TCP worker connected to master {}:{}.", impl_->host, impl_->port);
            }
        }

        void TcpTransport::stop()
        {
            bool expected = true;
            if (!impl_->running.compare_exchange_strong(expected, false))
                return;

            // First shutdown() every socket so threads blocked in recv()/accept()
            // wake up — close() alone would not unblock them, and we would then
            // deadlock joining the reader threads.
            shutdown_sock(impl_->listen_sock);
            for (auto& s : impl_->peers)
                shutdown_sock(s);

            if (impl_->accept_thread.joinable())
                impl_->accept_thread.join();
            for (auto& t : impl_->readers)
                if (t.joinable()) t.join();
            impl_->readers.clear();

            // Now it is safe to close the descriptors.
            close_sock(impl_->listen_sock);
            impl_->listen_sock = INVALID_SOCK;
            for (auto& s : impl_->peers)
            {
                close_sock(s);
                s = INVALID_SOCK;
            }
        }

        void TcpTransport::send(int peer_id, MsgType type, std::span<const std::byte> payload)
        {
            if (!impl_->running.load()) return;

            if (impl_->is_master)
            {
                if (peer_id < 0) // broadcast
                {
                    for (size_t i = 0; i < impl_->peers.size(); ++i)
                        impl_->send_to(impl_->peers[i], *impl_->send_mtx[i], type, payload);
                }
                else if (peer_id < static_cast<int>(impl_->peers.size()))
                {
                    impl_->send_to(impl_->peers[static_cast<size_t>(peer_id)],
                                   *impl_->send_mtx[static_cast<size_t>(peer_id)], type, payload);
                }
            }
            else
            {
                impl_->send_to(impl_->peers[0], *impl_->send_mtx[0], type, payload);
            }
        }

        int TcpTransport::peer_count() const
        {
            return impl_->is_master ? impl_->worker_count : 1;
        }

        uint16_t TcpTransport::bound_port() const { return impl_->port; }

    } // namespace Distributed
} // namespace SushiRuntime
