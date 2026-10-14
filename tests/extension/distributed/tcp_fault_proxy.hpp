/**************************************************************************/
/* tcp_fault_proxy.hpp                                                    */
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
 * @file tcp_fault_proxy.hpp
 * @brief Declares TCPFaultProxy, a loopback proxy that injects faults between worker and master.
 * @author Mustafa Garip
 * @see tests/extension/distributed/README.md
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/endpoint.hpp>

#include "socket.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Holds the helpers that only the tests of the distributed module use. */
        namespace Test
        {
            /** @brief Names one of the two byte streams of a proxied connection. */
            enum class FlowDirection : std::uint8_t
            {
                WorkerToMaster, /**< The bytes the worker sends. */
                MasterToWorker, /**< The bytes the master sends. */
            };

            /** @brief Counts the directions; every table indexed by one has this size. */
            inline constexpr std::size_t K_FLOW_DIRECTIONS = 2;

            /** @brief Names what the proxy does with the bytes of one direction. */
            enum class FaultKind : std::uint8_t
            {
                Pass,     /**< Forwards every byte as it arrives. */
                Stall,    /**< Holds every byte and the end of the stream until the next fault. */
                Cut,      /**< Closes every connection and refuses new ones. */
                Throttle, /**< Forwards at most Fault::amount bytes per second. */
                Truncate, /**< Forwards Fault::amount more bytes, closes, then acts as Cut. */
            };

            /** @brief Counts the fault kinds; the rule table has one entry for each. */
            inline constexpr std::size_t K_FAULT_KINDS = 5;

            /**
             * @brief Holds one fault: its kind and the number the kind reads.
             */
            struct Fault
            {
                /** @brief Holds what happens to the bytes. */
                FaultKind kind = FaultKind::Pass;

                /** @brief Holds bytes per second for Throttle, bytes left for Truncate, else 0. */
                std::uint64_t amount = 0;

                /** @brief Returns the fault that forwards every byte. */
                [[nodiscard]] static constexpr Fault pass() noexcept
                {
                    return Fault{FaultKind::Pass, 0};
                }

                /** @brief Returns the fault that holds every byte, as a silent link does. */
                [[nodiscard]] static constexpr Fault stall() noexcept
                {
                    return Fault{FaultKind::Stall, 0};
                }

                /** @brief Returns the fault that closes every connection at once. */
                [[nodiscard]] static constexpr Fault cut() noexcept
                {
                    return Fault{FaultKind::Cut, 0};
                }

                /**
                 * @brief Returns the fault that paces a direction.
                 * @param bytes_per_second Above 0; the pace holds from the call that sets it.
                 */
                [[nodiscard]] static constexpr Fault throttle(
                    std::uint64_t bytes_per_second) noexcept
                {
                    return Fault{FaultKind::Throttle, bytes_per_second};
                }

                /**
                 * @brief Returns the fault that closes a direction in the middle of its stream.
                 * @param bytes The bytes still forwarded, counted from the call that sets it.
                 */
                [[nodiscard]] static constexpr Fault truncate_after(std::uint64_t bytes) noexcept
                {
                    return Fault{FaultKind::Truncate, bytes};
                }
            };

            /** @brief Holds the fault rules and the state they read; only the proxy uses it. */
            namespace Detail
            {
                /** @brief Names the clock the proxy paces a throttled direction with. */
                using ProxyClock = std::chrono::steady_clock;

                /** @brief Holds the slices one second of a throttled direction is cut into. */
                inline constexpr std::uint64_t K_THROTTLE_SLICES_PER_SECOND = 100;

                /** @brief Holds the nanoseconds of one second. */
                inline constexpr std::uint64_t K_NANOSECONDS_PER_SECOND = 1000000000;

                /**
                 * @brief Holds the fault of one direction and what the proxy counted for it.
                 */
                struct Flow
                {
                    /** @brief Holds the fault in force. */
                    Fault fault;

                    /** @brief Holds the time the fault was set; a throttle paces from it. */
                    ProxyClock::time_point armed_at;

                    /** @brief Counts the bytes forwarded since the fault was set. */
                    std::uint64_t metered_bytes = 0;

                    /** @brief Counts the bytes forwarded since the proxy started. */
                    std::uint64_t forwarded_bytes = 0;
                };

                /** @brief Names what a pump does next with the bytes it holds. */
                enum class AdmissionStep : std::uint8_t
                {
                    Forward,   /**< Write Admission::bytes of them now. */
                    Hold,      /**< Wait until a fault changes. */
                    HoldUntil, /**< Wait until Admission::until, or until a fault changes. */
                    Sever,     /**< Close the connection. */
                };

                /**
                 * @brief Carries the answer of a fault rule.
                 */
                struct Admission
                {
                    /** @brief Holds what the pump does next. */
                    AdmissionStep step = AdmissionStep::Forward;

                    /** @brief Holds the bytes to write after Forward. */
                    std::size_t bytes = 0;

                    /** @brief Holds the time to wait for after HoldUntil. */
                    ProxyClock::time_point until;
                };

                /**
                 * @brief Names a fault rule: it answers for @p wanted held bytes at @p now.
                 *
                 * A rule runs under the proxy's mutex and may change the flow it is given.
                 */
                using FaultRule = Admission (*)(Flow& flow, std::size_t wanted,
                                                ProxyClock::time_point now) noexcept;

                /** @brief Forwards everything. */
                [[nodiscard]] inline Admission admit_pass(Flow& /*flow*/, std::size_t wanted,
                                                          ProxyClock::time_point /*now*/) noexcept
                {
                    return Admission{AdmissionStep::Forward, wanted, ProxyClock::time_point()};
                }

                /** @brief Holds everything. */
                [[nodiscard]] inline Admission admit_stall(Flow& /*flow*/, std::size_t /*wanted*/,
                                                           ProxyClock::time_point /*now*/) noexcept
                {
                    return Admission{AdmissionStep::Hold, 0, ProxyClock::time_point()};
                }

                /** @brief Closes the connection. */
                [[nodiscard]] inline Admission admit_cut(Flow& /*flow*/, std::size_t /*wanted*/,
                                                         ProxyClock::time_point /*now*/) noexcept
                {
                    return Admission{AdmissionStep::Sever, 0, ProxyClock::time_point()};
                }

                /** @brief Forwards one slice once the bytes metered so far are due at the rate. */
                [[nodiscard]] inline Admission admit_throttle(Flow& flow, std::size_t wanted,
                                                              ProxyClock::time_point now) noexcept
                {
                    const std::uint64_t rate = std::max<std::uint64_t>(flow.fault.amount, 1);
                    const ProxyClock::time_point due =
                        flow.armed_at +
                        std::chrono::duration_cast<ProxyClock::duration>(std::chrono::nanoseconds(
                            static_cast<std::int64_t>(flow.metered_bytes *
                                                      K_NANOSECONDS_PER_SECOND / rate)));
                    if (now < due)
                        return Admission{AdmissionStep::HoldUntil, 0, due};

                    const std::uint64_t slice =
                        std::max<std::uint64_t>(rate / K_THROTTLE_SLICES_PER_SECOND, 1);
                    const std::size_t granted =
                        static_cast<std::size_t>(std::min<std::uint64_t>(wanted, slice));
                    flow.metered_bytes += granted;
                    return Admission{AdmissionStep::Forward, granted, ProxyClock::time_point()};
                }

                /** @brief Forwards what is left of the allowance, then turns the fault into Cut. */
                [[nodiscard]] inline Admission admit_truncate(
                    Flow& flow, std::size_t wanted, ProxyClock::time_point /*now*/) noexcept
                {
                    const std::size_t granted = static_cast<std::size_t>(
                        std::min<std::uint64_t>(wanted, flow.fault.amount));
                    flow.fault.amount -= granted;
                    if (flow.fault.amount == 0)
                        flow.fault = Fault::cut();
                    if (granted == 0 && wanted != 0)
                        return Admission{AdmissionStep::Sever, 0, ProxyClock::time_point()};
                    return Admission{AdmissionStep::Forward, granted, ProxyClock::time_point()};
                }

                /** @brief Holds the rule of each fault kind, indexed by the kind's value. */
                inline constexpr std::array<FaultRule, K_FAULT_KINDS> K_FAULT_RULES = {
                    &admit_pass, &admit_stall, &admit_cut, &admit_throttle, &admit_truncate};
            } // namespace Detail

            /**
             * @brief Forwards TCP connections to a master and applies one fault per direction.
             *
             * A worker connects to endpoint(); the proxy opens a connection to the master for
             * it and runs one pump thread per direction. A fault holds for every connection,
             * open or opened later, until the next set_fault().
             * @see tests/extension/distributed/README.md
             */
            class TCPFaultProxy final
            {
                public:
                    /**
                     * @brief Listens on a free loopback port and starts accepting.
                     * @param master Where each accepted connection is forwarded to.
                     */
                    explicit TCPFaultProxy(Endpoint master) : master_(std::move(master))
                    {
                        started_ = listener_.listen(Endpoint{"127.0.0.1", 0}, K_BACKLOG);
                        if (started_.is_ok())
                            acceptor_ = std::thread([this] { accept_loop(); });
                    }

                    /** @brief Closes every connection and joins every thread. */
                    ~TCPFaultProxy()
                    {
                        stop();
                    }

                    TCPFaultProxy(const TCPFaultProxy&) = delete;
                    TCPFaultProxy& operator=(const TCPFaultProxy&) = delete;
                    TCPFaultProxy(TCPFaultProxy&&) = delete;
                    TCPFaultProxy& operator=(TCPFaultProxy&&) = delete;

                    /** @brief Returns how the listener started; the proxy runs only after Ok. */
                    [[nodiscard]] const SocketResult& started() const noexcept
                    {
                        return started_;
                    }

                    /**
                     * @brief Returns the address a worker connects to in place of the master's.
                     * @pre started() is Ok.
                     */
                    [[nodiscard]] Endpoint endpoint() const
                    {
                        return listener_.local_endpoint();
                    }

                    /** @brief Forwards the connections accepted from here on to another master. */
                    void retarget(Endpoint master)
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        master_ = std::move(master);
                    }

                    /**
                     * @brief Sets the fault of one direction and restarts its byte count.
                     *
                     * Cut in either direction closes every open connection before this returns.
                     */
                    void set_fault(FlowDirection direction, Fault fault)
                    {
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            Detail::Flow& flow = flows_[static_cast<std::size_t>(direction)];
                            flow.fault = fault;
                            flow.armed_at = Detail::ProxyClock::now();
                            flow.metered_bytes = 0;
                            if (fault.kind == FaultKind::Cut)
                                for (const std::unique_ptr<Link>& link : links_)
                                    link->sever();
                        }
                        changed_.notify_all();
                    }

                    /** @brief Sets the same fault for both directions. */
                    void set_fault(Fault fault)
                    {
                        set_fault(FlowDirection::WorkerToMaster, fault);
                        set_fault(FlowDirection::MasterToWorker, fault);
                    }

                    /** @brief Returns the bytes forwarded in a direction since the start. */
                    [[nodiscard]] std::uint64_t forwarded_bytes(FlowDirection direction) const
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        return flows_[static_cast<std::size_t>(direction)].forwarded_bytes;
                    }

                    /** @brief Stops accepting, closes every connection and joins every thread. */
                    void stop() noexcept
                    {
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            is_stopping_ = true;
                        }
                        changed_.notify_all();
                        if (acceptor_.joinable())
                            acceptor_.join();

                        // The acceptor is joined, so no connection is added behind this swap.
                        std::vector<std::unique_ptr<Link>> links;
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            links.swap(links_);
                        }
                        for (const std::unique_ptr<Link>& link : links)
                            link->sever();
                        for (const std::unique_ptr<Link>& link : links)
                            link->join();
                        listener_.close();
                    }

                private:
                    /** @brief Holds the pending connections the OS may queue for the proxy. */
                    static constexpr int K_BACKLOG = 16;

                    /** @brief Holds the bytes a pump reads at most before it forwards them. */
                    static constexpr std::size_t K_PUMP_BYTES = 64 * 1024;

                    /** @brief Holds how long one accept() waits before the stop flag is read. */
                    static constexpr std::chrono::milliseconds K_ACCEPT_POLL{20};

                    /** @brief Bounds the connect to the master for one accepted connection. */
                    static constexpr std::chrono::milliseconds K_CONNECT_LIMIT{1000};

                    /** @brief Makes a read return with what the OS already holds. */
                    static constexpr std::chrono::milliseconds K_NO_WAIT{0};

                    /**
                     * @brief Holds one proxied connection: its two sockets and its two pumps.
                     */
                    struct Link
                    {
                        /** @brief Takes the accepted socket and the one opened to the master. */
                        Link(Socket worker, Socket master) noexcept
                            : worker_side(std::move(worker)), master_side(std::move(master))
                        {

                        }

                        Link(const Link&) = delete;
                        Link& operator=(const Link&) = delete;
                        Link(Link&&) = delete;
                        Link& operator=(Link&&) = delete;

                        /** @brief Ends both sockets, so a pump waiting in a socket call returns. */
                        void sever() noexcept
                        {
                            is_severed.store(true);
                            worker_side.shutdown();
                            master_side.shutdown();
                        }

                        /** @brief Joins the pumps that were started. */
                        void join()
                        {
                            for (std::thread& pump : pumps)
                                if (pump.joinable())
                                    pump.join();
                        }

                        /** @brief Holds the connection the proxy accepted. */
                        Socket worker_side;

                        /** @brief Holds the connection the proxy opened to the master. */
                        Socket master_side;

                        /** @brief Holds the pump of each direction, indexed by FlowDirection. */
                        std::array<std::thread, K_FLOW_DIRECTIONS> pumps;

                        /** @brief Counts the pumps that have not ended. */
                        std::atomic<std::size_t> running{K_FLOW_DIRECTIONS};

                        /** @brief Set by sever(); a pump holding bytes then gives up. */
                        std::atomic<bool> is_severed{false};
                    };

                    /** @brief Returns whether a direction's fault is Cut; needs the mutex. */
                    [[nodiscard]] bool is_cut_locked() const noexcept
                    {
                        return std::any_of(flows_.begin(), flows_.end(),
                                           [](const Detail::Flow& flow)
                                           {
                                               return flow.fault.kind == FaultKind::Cut;
                                           });
                    }

                    /** @brief Returns whether a direction's fault is Cut. */
                    [[nodiscard]] bool is_cut() const
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        return is_cut_locked();
                    }

                    /**
                     * @brief Waits for a time, or for stop().
                     * @return True once stop() has begun.
                     */
                    [[nodiscard]] bool wait_stop_for(std::chrono::milliseconds time)
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        return changed_.wait_for(lock, time, [this] { return is_stopping_; });
                    }

                    /** @brief Runs the accept thread: reap, accept, open a link. */
                    void accept_loop()
                    {
                        while (!wait_stop_for(K_NO_WAIT))
                        {
                            reap_finished();

                            Socket accepted;
                            const SocketResult result = listener_.accept(accepted, K_ACCEPT_POLL);
                            if (result.is_ok())
                                open_link(std::move(accepted));
                            else if (result.status != SocketStatus::Timeout &&
                                     wait_stop_for(K_ACCEPT_POLL))
                                return;
                        }
                    }

                    /** @brief Joins and destroys every link whose two pumps have ended. */
                    void reap_finished()
                    {
                        std::vector<std::unique_ptr<Link>> finished;
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            const auto first_finished = std::stable_partition(
                                links_.begin(), links_.end(),
                                [](const std::unique_ptr<Link>& link)
                                {
                                    return link->running.load() != 0;
                                });
                            std::move(first_finished, links_.end(), std::back_inserter(finished));
                            links_.erase(first_finished, links_.end());
                        }
                        for (const std::unique_ptr<Link>& link : finished)
                            link->join();
                    }

                    /**
                     * @brief Connects an accepted socket to the master and starts its two pumps.
                     *
                     * The socket is closed when a direction is cut or the master refuses.
                     */
                    void open_link(Socket worker_side)
                    {
                        Endpoint master;
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            if (is_stopping_ || is_cut_locked())
                                return;
                            master = master_;
                        }

                        Socket master_side;
                        if (!master_side.connect(master, K_CONNECT_LIMIT).is_ok())
                            return;

                        auto link =
                            std::make_unique<Link>(std::move(worker_side), std::move(master_side));
                        Link& opened = *link;
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            links_.push_back(std::move(link));
                            if (is_cut_locked())
                                opened.sever();
                        }
                        for (std::size_t index = 0; index < K_FLOW_DIRECTIONS; ++index)
                            opened.pumps[index] = std::thread([this, &opened, index]
                            {
                                pump(opened, static_cast<FlowDirection>(index));
                            });
                    }

                    /** @brief Runs one direction of a link until its stream or the link ends. */
                    void pump(Link& link, FlowDirection direction)
                    {
                        const bool is_upward = direction == FlowDirection::WorkerToMaster;
                        Socket& source = is_upward ? link.worker_side : link.master_side;
                        Socket& sink = is_upward ? link.master_side : link.worker_side;

                        std::vector<std::byte> buffer(K_PUMP_BYTES);
                        bool is_open = true;
                        while (is_open)
                        {
                            std::size_t held = 0;
                            const bool has_more = read_available(source, buffer, held);
                            is_open = forward(link, direction, sink,
                                              SushiRuntime::span<const std::byte>(buffer.data(),
                                                                                  held)) &&
                                      has_more;
                        }

                        // Taking the mutex orders the flag before the other pump's wait.
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            link.sever();
                        }
                        changed_.notify_all();
                        link.running.fetch_sub(1);
                    }

                    /**
                     * @brief Waits for one byte, then takes what else the OS already holds.
                     * @param held Receives the number of bytes read into @p buffer.
                     * @return False once the stream has ended or failed.
                     */
                    [[nodiscard]] static bool read_available(Socket& source,
                                                             std::vector<std::byte>& buffer,
                                                             std::size_t& held)
                    {
                        const SocketResult first =
                            source.read_exact({buffer.data(), 1}, K_WAIT_FOREVER);
                        held = first.transferred;
                        if (!first.is_ok())
                            return false;

                        const SocketResult rest =
                            source.read_exact({buffer.data() + 1, buffer.size() - 1}, K_NO_WAIT);
                        held += rest.transferred;
                        return rest.is_ok() || rest.status == SocketStatus::Timeout;
                    }

                    /**
                     * @brief Writes held bytes to the other side as the direction's fault allows.
                     *
                     * An empty range is the end of the stream, which a stall holds as well.
                     * @return False when the link must close.
                     */
                    [[nodiscard]] bool forward(const Link& link, FlowDirection direction,
                                               Socket& sink,
                                               SushiRuntime::span<const std::byte> bytes)
                    {
                        std::size_t done = 0;
                        do
                        {
                            const std::size_t granted =
                                admit(link, direction, bytes.size() - done);
                            if (granted == 0)
                                return false;
                            if (!sink.write_all({bytes.data() + done, granted}, K_WAIT_FOREVER)
                                     .is_ok())
                                return false;
                            done += granted;
                        } while (done < bytes.size());

                        return !is_cut();
                    }

                    /**
                     * @brief Asks the direction's rule how many held bytes may be written now.
                     *
                     * Blocks while the rule holds the bytes.
                     * @return 0 when the link must close, which is also the answer for 0 bytes.
                     */
                    [[nodiscard]] std::size_t admit(const Link& link, FlowDirection direction,
                                                    std::size_t wanted)
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        Detail::Flow& flow = flows_[static_cast<std::size_t>(direction)];
                        for (;;)
                        {
                            if (is_stopping_ || link.is_severed.load())
                                return 0;

                            const Detail::FaultRule rule =
                                Detail::K_FAULT_RULES[static_cast<std::size_t>(flow.fault.kind)];
                            const Detail::Admission admission =
                                rule(flow, wanted, Detail::ProxyClock::now());
                            if (admission.step == Detail::AdmissionStep::Sever)
                                return 0;
                            if (admission.step == Detail::AdmissionStep::Forward)
                            {
                                flow.forwarded_bytes += admission.bytes;
                                return admission.bytes;
                            }

                            if (admission.step == Detail::AdmissionStep::Hold)
                                changed_.wait(lock);
                            else
                                changed_.wait_until(lock, admission.until);
                        }
                    }

                    /** @brief Holds the listening socket workers connect to. */
                    Listener listener_;

                    /** @brief Holds how the listener started. */
                    SocketResult started_;

                    /** @brief Holds the accept thread. */
                    std::thread acceptor_;

                    /** @brief Guards every member below. */
                    mutable std::mutex mutex_;

                    /** @brief Wakes the pumps and the acceptor on a fault, a sever and stop(). */
                    std::condition_variable changed_;

                    /** @brief Holds the master that new links connect to. */
                    Endpoint master_;

                    /** @brief Holds the fault and the counts of each direction. */
                    std::array<Detail::Flow, K_FLOW_DIRECTIONS> flows_;

                    /** @brief Holds the links that have not been reaped. */
                    std::vector<std::unique_ptr<Link>> links_;

                    /** @brief Set by stop(). */
                    bool is_stopping_ = false;
            };
        } // namespace Test
    } // namespace Distributed
} // namespace SushiRuntime
