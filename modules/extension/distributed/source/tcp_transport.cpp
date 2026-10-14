/**************************************************************************/
/* tcp_transport.cpp                                                      */
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
 * @file tcp_transport.cpp
 * @brief Accepts or connects TCP peers and keeps the table of their connections.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include <SushiRuntime/distributed/tcp_transport.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/network_socket/socket.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include "peer_connection.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds how long one accept() waits before the acceptor looks around. */
            constexpr std::chrono::milliseconds K_ACCEPT_POLL{100};

            /** @brief Holds the wait after an accept() the OS refused. */
            constexpr std::chrono::milliseconds K_ACCEPT_RETRY{200};

            /** @brief Bounds the write of a REJECT(busy) to a connection that gets no slot. */
            constexpr std::chrono::milliseconds K_REFUSAL_WRITE_LIMIT{250};

            /** @brief Holds the spacing of the repeated lines of one failing loop. */
            constexpr std::uint64_t K_LOGGED_EVERY = 10;

            /** @brief Returns whether a loop logs its n-th failure: the first, then every tenth. */
            constexpr bool is_logged_attempt(std::uint64_t attempt) noexcept
            {
                return attempt == 1 || attempt % K_LOGGED_EVERY == 0;
            }

            /** @brief Returns whether a host is 127.0.0.0/8, ::1 or localhost. */
            bool is_loopback_host(std::string_view host)
            {
                std::string lowered(host);
                std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                               [](unsigned char letter)
                               {
                                   return static_cast<char>(std::tolower(letter));
                               });
                if (lowered == "localhost" || lowered == "::1")
                    return true;

                const bool is_dotted_quad =
                    std::count(lowered.begin(), lowered.end(), '.') == 3 &&
                    lowered.find_first_not_of("0123456789.") == std::string::npos;
                return is_dotted_quad && lowered.compare(0, 4, "127.") == 0;
            }

            /** @brief Formats a failed connect as the cause of a "cannot reach master" line. */
            std::string describe_unreachable(const Endpoint& master, const SocketResult& result)
            {
                std::string text = "cannot reach master " + master.to_string();
                if (!result.address.empty())
                    text.append(" (").append(result.address).append(")");
                text.append(": ").append(result.cause);
                if (result.os_error != 0)
                    text.append(" (os error ").append(std::to_string(result.os_error)).append(")");
                return text;
            }

            /**
             * @brief Holds a transport's connections by PeerID and relays their events.
             *
             * One mutex guards the table. It is held for a lookup or an update only, never
             * across an event, a socket call, a join or a call into a connection's outbox.
             */
            class PeerTable final : public TransportEvents
            {
                public:
                    /** @brief Builds an empty table whose connections take @p options. */
                    explicit PeerTable(const PeerConnectionOptions& options) : options_(options)
                    {

                    }

                    /**
                     * @brief Names the receiver every event is relayed to.
                     * @param receiver Outlives stop_all(); set before the first open().
                     */
                    void set_receiver(TransportEvents& receiver) noexcept
                    {
                        receiver_ = &receiver;
                    }

                    /**
                     * @brief Gives a connected socket the next id and starts its two threads.
                     * @return False once shut() was called; the socket is then closed.
                     */
                    bool open(Socket socket, PeerDescription info)
                    {
                        const PeerID peer{next_peer_.fetch_add(1)};
                        std::shared_ptr<PeerConnection> connection =
                            std::make_shared<PeerConnection>(peer, std::move(socket),
                                                             std::move(info), options_, *this);
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (is_shut_)
                                return false;
                            peers_.emplace(peer.value, Entry{connection, true});
                            ++open_peers_;
                        }
                        connection->start();
                        if (connection->is_finished())
                            reap_finished();
                        return true;
                    }

                    /** @brief Returns the open connection of a peer, or null. */
                    [[nodiscard]] std::shared_ptr<PeerConnection> find(PeerID peer)
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        const auto found = peers_.find(peer.value);
                        if (found == peers_.end() || !found->second.is_open)
                            return nullptr;
                        return found->second.connection;
                    }

                    /** @brief Returns the number of connections that have reported no loss. */
                    [[nodiscard]] std::size_t open_count()
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        return open_peers_;
                    }

                    /**
                     * @brief Joins and destroys every connection whose threads have ended.
                     * @pre The caller is the transport's accept or connect thread.
                     */
                    void reap_finished()
                    {
                        while (std::shared_ptr<PeerConnection> finished = take(is_finished))
                        {
                            finished->stop();
                            wake_.notify_all();
                        }
                    }

                    /**
                     * @brief Joins and destroys every connection whose loss was reported.
                     *
                     * Blocks until each on_peer_lost() in progress has returned.
                     * @pre The caller is the transport's accept or connect thread.
                     */
                    void join_lost()
                    {
                        while (std::shared_ptr<PeerConnection> lost = take(is_lost))
                            lost->stop();
                    }

                    /** @brief Refuses every later open() and wakes the waits below. */
                    void shut() noexcept
                    {
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            is_shut_ = true;
                        }
                        wake_.notify_all();
                    }

                    /** @brief Returns whether shut() was called. */
                    [[nodiscard]] bool is_shut()
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        return is_shut_;
                    }

                    /**
                     * @brief Waits for a time, or for shut().
                     * @return True when the table is shut.
                     */
                    [[nodiscard]] bool wait_shut_for(std::chrono::milliseconds time)
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        return wake_.wait_for(lock, time, [this] { return is_shut_; });
                    }

                    /**
                     * @brief Waits until no connection is open, or for shut().
                     * @return True when the table is shut.
                     */
                    [[nodiscard]] bool wait_until_no_peer()
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        wake_.wait(lock, [this] { return is_shut_ || open_peers_ == 0; });
                        return is_shut_;
                    }

                    /**
                     * @brief Stops every connection, with no on_peer_lost().
                     * @pre shut() was called and the accept or connect thread has ended.
                     */
                    void stop_all() noexcept
                    {
                        for (;;)
                        {
                            std::shared_ptr<PeerConnection> connection;
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                if (peers_.empty())
                                    return;
                                connection = std::move(peers_.begin()->second.connection);
                                peers_.erase(peers_.begin());
                            }
                            connection->stop();
                            std::lock_guard<std::mutex> lock(mutex_);
                            retired_.bytes_sent += connection->bytes_sent();
                            retired_.bytes_received += connection->bytes_received();
                        }
                    }

                    /** @brief Returns the bytes of every connection the table has held. */
                    [[nodiscard]] TransportStatistics totals() const noexcept
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        TransportStatistics sum = retired_;
                        for (const auto& item : peers_)
                        {
                            sum.bytes_sent += item.second.connection->bytes_sent();
                            sum.bytes_received += item.second.connection->bytes_received();
                        }
                        return sum;
                    }

                    /** @brief Relays the event. */
                    void on_peer_connected(PeerID peer, const PeerDescription& info) override
                    {
                        receiver_->on_peer_connected(peer, info);
                    }

                    /** @brief Relays the frame. */
                    void on_frame(PeerID peer, MessageType type, std::uint8_t flags,
                                  SushiRuntime::span<const std::byte> payload) override
                    {
                        receiver_->on_frame(peer, type, flags, payload);
                    }

                    /** @brief Marks the peer closed, so sends return UnknownPeer, then relays. */
                    void on_peer_lost(PeerID peer, const PeerLoss& loss) override
                    {
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            const auto found = peers_.find(peer.value);
                            if (found != peers_.end() && found->second.is_open)
                            {
                                found->second.is_open = false;
                                --open_peers_;
                            }
                        }
                        receiver_->on_peer_lost(peer, loss);
                        wake_.notify_all();
                    }

                    /** @brief Relays the event; no connection makes it. */
                    void on_connect_abandoned(const std::string& cause) override
                    {
                        receiver_->on_connect_abandoned(cause);
                    }

                private:
                    /** @brief Holds one connection and whether it has reported no loss yet. */
                    struct Entry
                    {
                        std::shared_ptr<PeerConnection> connection; /**< The connection. */
                        bool is_open = true; /**< Cleared when on_peer_lost() begins. */
                    };

                    /** @brief Returns whether both threads of an entry's connection have ended. */
                    static bool is_finished(const Entry& entry) noexcept
                    {
                        return entry.connection->is_finished();
                    }

                    /** @brief Returns whether an entry's connection has reported its loss. */
                    static bool is_lost(const Entry& entry) noexcept
                    {
                        return !entry.is_open;
                    }

                    /**
                     * @brief Removes the first connection a test accepts, or returns null.
                     *
                     * The caller stops what it gets. The byte counts move to retired_ here; a
                     * connection taken by is_lost may still add a few bytes afterwards.
                     */
                    std::shared_ptr<PeerConnection> take(bool (*is_due)(const Entry&) noexcept)
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        for (auto entry = peers_.begin(); entry != peers_.end(); ++entry)
                        {
                            if (!is_due(entry->second))
                                continue;
                            std::shared_ptr<PeerConnection> taken =
                                std::move(entry->second.connection);
                            if (entry->second.is_open)
                                --open_peers_;
                            retired_.bytes_sent += taken->bytes_sent();
                            retired_.bytes_received += taken->bytes_received();
                            peers_.erase(entry);
                            return taken;
                        }
                        return nullptr;
                    }

                    /// @brief The settings every connection takes.
                    PeerConnectionOptions options_;

                    /// @brief Receives the relayed events.
                    TransportEvents* receiver_ = nullptr;

                    /// @brief The id the next connection gets; ids start at 1.
                    std::atomic<std::uint32_t> next_peer_{1};

                    /// @brief Guards every member below.
                    mutable std::mutex mutex_;

                    /// @brief Wakes the waits on a loss, a reap and shut().
                    std::condition_variable wake_;

                    /// @brief The connections by PeerID::value.
                    std::unordered_map<std::uint32_t, Entry> peers_;

                    /// @brief Counts the entries whose is_open is set.
                    std::size_t open_peers_ = 0;

                    /// @brief Sums the bytes of the connections that left the table.
                    TransportStatistics retired_;

                    /// @brief Set by shut().
                    bool is_shut_ = false;
            };

            /** @brief Builds the settings a master gives each connection. */
            PeerConnectionOptions connection_options(const TCPMasterOptions& options)
            {
                PeerConnectionOptions result;
                result.control_queue_frames = options.control_queue_frames;
                result.write_idle_timeout = options.write_idle_timeout;
                return result;
            }

            /** @brief Builds the settings a worker gives its connection. */
            PeerConnectionOptions connection_options(const TCPWorkerOptions& options)
            {
                PeerConnectionOptions result;
                result.control_queue_frames = options.control_queue_frames;
                result.write_idle_timeout = options.write_idle_timeout;
                return result;
            }

            /** @brief Returns the numeric address of a socket's peer, or a fallback name. */
            std::string peer_name(const Socket& socket, const std::string& fallback)
            {
                Endpoint numeric;
                return socket.peer_endpoint(numeric).is_ok() ? numeric.to_string() : fallback;
            }

            /** @brief Writes REJECT(busy) to a connection that gets no slot and ends it. */
            void refuse_busy(Socket& socket, const std::string& name, std::size_t open_peers,
                             std::uint64_t refusal)
            {
                Wire::RejectMessage reject;
                reject.reason = Wire::RejectReason::Busy;
                reject.sender_version = Wire::K_PROTOCOL_VERSION;
                reject.text = "the master takes no more peers";

                std::vector<std::byte> frame;
                const bool is_sent =
                    Wire::append_frame(reject, 0, frame) == Wire::CodecStatus::Ok &&
                    socket.write_all({frame.data(), frame.size()}, K_REFUSAL_WRITE_LIMIT).is_ok();
                socket.shutdown();

                if (is_logged_attempt(refusal))
                    SR_LOG_WARN("{} refused: {} peers are open, the limit; refusal {}, "
                                "REJECT(busy) {}",
                                name, open_peers, refusal,
                                is_sent ? "sent" : "not sent");
            }
        } // namespace

        /**
         * @brief Holds one endpoint's role, options, listener, peer table and service thread.
         */
        struct TCPTransport::Impl
        {
            /** @brief Names which side of the session the endpoint is. */
            enum class Role : std::uint8_t
            {
                Master, /**< Listens and accepts. */
                Worker, /**< Connects to one master. */
            };

            /** @brief Names where the endpoint is in its one start and one stop. */
            enum class Phase : std::uint8_t
            {
                Idle,    /**< start() has not run. */
                Running, /**< start() returned Ok and stop() has not begun. */
                Stopped, /**< stop() has begun, or start() failed. */
            };

            /** @brief Builds a master endpoint. */
            explicit Impl(const TCPMasterOptions& options)
                : role(Role::Master), master_options(options),
                  exposure(is_loopback_host(options.bind.host) ? TransportExposure::Loopback
                                                               : TransportExposure::Network),
                  table(connection_options(options))
            {

            }

            /** @brief Builds a worker endpoint. */
            Impl(Endpoint master_endpoint, const TCPWorkerOptions& options)
                : role(Role::Worker), worker_options(options), master(std::move(master_endpoint)),
                  exposure(is_loopback_host(master.host) ? TransportExposure::Loopback
                                                         : TransportExposure::Network),
                  table(connection_options(options))
            {

            }

            /**
             * @brief Binds and listens.
             * @param result Receives the bound `address:port` as its message, or the refusal.
             */
            bool open_listener(TransportResult& result)
            {
                if (master_options.bind.host.empty() || master_options.max_peers == 0)
                {
                    result.status = TransportStatus::InvalidArgument;
                    result.message = "cannot listen on '" + master_options.bind.to_string() +
                                     "': the bind host is empty or max_peers is 0";
                    return false;
                }
                const SocketResult listening =
                    listener.listen(master_options.bind, master_options.backlog);
                if (listening.is_ok())
                {
                    result.message = listener.local_endpoint().to_string();
                    return true;
                }
                result.status = TransportStatus::BindFailed;
                result.message = "cannot listen on " + master_options.bind.to_string() + ": " +
                                 describe(listening) +
                                 "; if another process holds the port, choose another or stop it";
                return false;
            }

            /** @brief Checks the master's address; fills @p result on a refusal. */
            bool check_master(TransportResult& result) const
            {
                if (!master.host.empty() && master.port != 0)
                    return true;
                result.status = TransportStatus::InvalidArgument;
                result.message = "cannot connect to '" + master.to_string() +
                                 "': the master needs a host and a port above 0";
                return false;
            }

            /** @brief Runs the master's service thread: reap, accept, admit or refuse. */
            void accept_loop()
            {
                const std::string local = listener.local_endpoint().to_string();
                std::uint64_t failures = 0;
                std::uint64_t refusals = 0;
                while (!table.is_shut())
                {
                    table.reap_finished();

                    Socket accepted;
                    const SocketResult result = listener.accept(accepted, K_ACCEPT_POLL);
                    if (result.status == SocketStatus::Timeout)
                        continue;
                    if (!result.is_ok())
                    {
                        if (is_logged_attempt(++failures))
                            SR_LOG_WARN("accept on {} failed: {}; failure {}, next try in {} ms",
                                        local, describe(result), failures,
                                        K_ACCEPT_RETRY.count());
                        if (table.wait_shut_for(K_ACCEPT_RETRY))
                            return;
                        continue;
                    }

                    const std::string name = peer_name(accepted, "an unnamed address");
                    const std::size_t open_peers = table.open_count();
                    if (open_peers >= master_options.max_peers)
                    {
                        refuse_busy(accepted, name, open_peers, ++refusals);
                        continue;
                    }
                    SR_LOG_DEBUG("{} accepted a connection from {}", local, name);
                    if (!table.open(std::move(accepted), PeerDescription{name}))
                        return;
                }
            }

            /** @brief Makes on_connect_abandoned() and logs what escapes it. */
            void abandon(const std::string& cause)
            {
                try
                {
                    events->on_connect_abandoned(cause);
                }
                catch (const std::exception& error)
                {
                    SR_LOG_ERROR("handler of the abandoned connection to master {} threw '{}'; "
                                 "the connector ends as planned",
                                 master.to_string(), error.what());
                }
                catch (...)
                {
                    SR_LOG_ERROR("handler of the abandoned connection to master {} threw an "
                                 "unknown exception; the connector ends as planned",
                                 master.to_string());
                }
            }

            /** @brief Runs the worker's service thread: connect, wait for the loss, back off. */
            void connect_loop()
            {
                using Clock = std::chrono::steady_clock;

                const std::string target = master.to_string();
                const Clock::time_point started = Clock::now();
                const bool has_deadline = worker_options.connect_timeout.count() > 0;
                bool was_connected = false;
                std::uint64_t attempt = 0;
                std::chrono::milliseconds backoff = worker_options.backoff_initial;

                while (!table.is_shut())
                {
                    ++attempt;
                    std::chrono::milliseconds wait = backoff;

                    Socket socket;
                    const SocketResult connected =
                        socket.connect(master, worker_options.attempt_timeout);
                    if (connected.is_ok())
                    {
                        const Clock::time_point opened = Clock::now();
                        was_connected = true;
                        const std::string name = peer_name(socket, target);
                        SR_LOG_DEBUG("connected to master {} ({}) at attempt {}", target, name,
                                     attempt);
                        if (!table.open(std::move(socket), PeerDescription{name}) ||
                            table.wait_until_no_peer())
                            return;
                        table.join_lost();

                        if (!worker_options.reconnect)
                        {
                            abandon("master " + target + " was lost and reconnecting is off");
                            return;
                        }
                        // A connection that ended early keeps the backoff it was reached with.
                        if (Clock::now() - opened >= worker_options.backoff_ceiling)
                        {
                            attempt = 0;
                            backoff = worker_options.backoff_initial;
                            wait = backoff;
                        }
                    }
                    else
                    {
                        const std::string cause = describe_unreachable(master, connected);
                        const Clock::duration waited = Clock::now() - started;
                        if (!was_connected && has_deadline)
                        {
                            if (waited >= worker_options.connect_timeout)
                            {
                                abandon(cause + "; gave up after " +
                                        std::to_string(worker_options.connect_timeout.count()) +
                                        " s and " + std::to_string(attempt) + " attempts");
                                return;
                            }
                            wait = std::min(
                                wait, std::chrono::duration_cast<std::chrono::milliseconds>(
                                          worker_options.connect_timeout - waited) +
                                          std::chrono::milliseconds(1));
                        }
                        if (is_logged_attempt(attempt))
                            SR_LOG_WARN("{}; attempt {}, next in {} ms", cause, attempt,
                                        wait.count());
                    }

                    if (table.wait_shut_for(wait))
                        return;
                    backoff = std::min(backoff * 2, worker_options.backoff_ceiling);
                }
            }

            /// @brief Which side the endpoint is.
            Role role;

            /// @brief The settings of a master endpoint.
            TCPMasterOptions master_options;

            /// @brief The settings of a worker endpoint.
            TCPWorkerOptions worker_options;

            /// @brief The master a worker endpoint connects to.
            Endpoint master;

            /// @brief Who can reach the endpoint, decided from the address at construction.
            TransportExposure exposure;

            /// @brief The connections and the relay of their events.
            PeerTable table;

            /// @brief The listening socket of a master endpoint.
            Listener listener;

            /// @brief The accept thread of a master or the connect thread of a worker.
            std::thread service;

            /// @brief Receives the events; set by start().
            TransportEvents* events = nullptr;

            /// @brief Serialises start() and stop().
            std::mutex lifecycle;

            /// @brief Where the endpoint is in its life; send() reads it without a lock.
            std::atomic<Phase> phase{Phase::Idle};

            /// @brief Set by the first start(), whatever its outcome.
            bool was_started = false;
        };

        TCPTransport::TCPTransport(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
        {

        }

        std::shared_ptr<TCPTransport> TCPTransport::make_master(TCPMasterOptions options)
        {
            return std::shared_ptr<TCPTransport>(
                new TCPTransport(std::make_unique<Impl>(options)));
        }

        std::shared_ptr<TCPTransport> TCPTransport::make_worker(Endpoint master,
                                                                TCPWorkerOptions options)
        {
            return std::shared_ptr<TCPTransport>(
                new TCPTransport(std::make_unique<Impl>(std::move(master), options)));
        }

        TCPTransport::~TCPTransport()
        {
            stop();
        }

        TransportResult TCPTransport::start(TransportEvents& events)
        {
            Impl& impl = *impl_;
            std::lock_guard<std::mutex> lock(impl.lifecycle);

            TransportResult result;
            if (impl.was_started)
            {
                result.status = TransportStatus::AlreadyStarted;
                result.message = "the TCP transport was started before; a transport starts once";
                return result;
            }
            impl.was_started = true;
            if (impl.phase.load() != Impl::Phase::Idle)
            {
                result.status = TransportStatus::Failed;
                result.message = "the TCP transport was stopped before it was started";
                return result;
            }

            const bool is_master = impl.role == Impl::Role::Master;
            if (!(is_master ? impl.open_listener(result) : impl.check_master(result)))
            {
                impl.phase.store(Impl::Phase::Stopped);
                return result;
            }

            impl.events = &events;
            impl.table.set_receiver(events);
            impl.phase.store(Impl::Phase::Running);
            try
            {
                impl.service = is_master ? std::thread([&impl] { impl.accept_loop(); })
                                         : std::thread([&impl] { impl.connect_loop(); });
            }
            catch (const std::system_error& error)
            {
                impl.phase.store(Impl::Phase::Stopped);
                impl.table.shut();
                impl.listener.close();
                result.status = TransportStatus::Failed;
                result.message = std::string("cannot start the TCP ") +
                                 (is_master ? "accept" : "connect") + " thread: " + error.what();
            }
            return result;
        }

        void TCPTransport::stop() noexcept
        {
            Impl& impl = *impl_;
            std::lock_guard<std::mutex> lock(impl.lifecycle);
            if (impl.phase.exchange(Impl::Phase::Stopped) != Impl::Phase::Running)
                return;

            impl.table.shut();
            if (impl.service.joinable())
                impl.service.join();
            impl.listener.close();
            impl.table.stop_all();
        }

        SendStatus TCPTransport::send(PeerID peer, MessageType type,
                                      SushiRuntime::span<const std::byte> payload)
        {
            if (impl_->phase.load(std::memory_order_acquire) != Impl::Phase::Running)
                return SendStatus::Stopped;
            const std::shared_ptr<PeerConnection> connection = impl_->table.find(peer);
            if (connection == nullptr)
                return SendStatus::UnknownPeer;
            return connection->send(type, payload);
        }

        SendStatus TCPTransport::send_stream(PeerID peer, std::unique_ptr<FrameSource> source)
        {
            if (impl_->phase.load(std::memory_order_acquire) != Impl::Phase::Running)
            {
                source.reset();
                return SendStatus::Stopped;
            }
            const std::shared_ptr<PeerConnection> connection = impl_->table.find(peer);
            if (connection == nullptr)
            {
                source.reset();
                return SendStatus::UnknownPeer;
            }
            return connection->send_stream(std::move(source));
        }

        void TCPTransport::close_peer(PeerID peer) noexcept
        {
            const std::shared_ptr<PeerConnection> connection = impl_->table.find(peer);
            if (connection != nullptr)
                connection->close();
        }

        TransportExposure TCPTransport::exposure() const noexcept
        {
            return impl_->exposure;
        }

        TransportStatistics TCPTransport::statistics() const noexcept
        {
            return impl_->table.totals();
        }

        Endpoint TCPTransport::local_endpoint() const
        {
            return impl_->listener.local_endpoint();
        }

    } // namespace Distributed
} // namespace SushiRuntime
