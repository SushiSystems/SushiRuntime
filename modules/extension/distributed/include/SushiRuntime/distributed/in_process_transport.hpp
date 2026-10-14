/**************************************************************************/
/* in_process_transport.hpp                                               */
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
 * @file in_process_transport.hpp
 * @brief Declares InProcessFabric and InProcessTransport, a transport inside one process.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names what the fabric does with one frame in flight.
         */
        enum class FaultAction : std::uint8_t
        {
            Deliver,   /**< Pass the frame to its destination. */
            Drop,      /**< Discard it, as a lost packet. */
            Duplicate, /**< Deliver it twice, as a repeated transmission. */
            Hold,      /**< Park it until flush_held(), as an arbitrarily slow link. */
        };

        /**
         * @brief Names the way a frame travels on the fabric.
         */
        enum class FrameDirection : std::uint8_t
        {
            MasterToWorker, /**< Sent by the master endpoint. */
            WorkerToMaster, /**< Sent by a worker endpoint. */
        };

        /**
         * @brief Describes one frame of protocol version 3 to a fault policy.
         */
        struct FaultedFrame
        {
            /** @brief Holds the way the frame travels. */
            FrameDirection direction = FrameDirection::MasterToWorker;

            /** @brief Holds the index of the worker endpoint that sends or receives it. */
            std::size_t worker_index = 0;

            /** @brief Holds the frame's type. */
            MessageType type = MessageType::Hello;

            /** @brief Holds the frame's flag byte. */
            std::uint8_t flags = 0;

            /** @brief Holds the payload; valid during the policy call only. */
            SushiRuntime::span<const std::byte> payload;
        };

        /**
         * @brief Decides the fate of each frame, control or stream, as it enters the fabric.
         *
         * Called on the thread that moves the frame, several at once, with no fabric lock
         * held. It does not call the fabric or an endpoint.
         */
        using FaultPolicy = std::function<FaultAction(const FaultedFrame& frame)>;

        /**
         * @brief Joins one master endpoint and any number of worker endpoints in one process.
         *
         * The endpoints share the fabric's state, so the fabric and its endpoints may be
         * destroyed in any order. Every member may be called from any thread.
         * @see modules/extension/distributed/README.md
         */
        class SUSHIRUNTIME_API InProcessFabric final
        {
            public:
                /** @brief Builds a fabric with no endpoint, no fault policy and no partition. */
                InProcessFabric();

                /** @brief Releases the fabric's share of the state. */
                ~InProcessFabric();

                InProcessFabric(const InProcessFabric&) = delete;
                InProcessFabric& operator=(const InProcessFabric&) = delete;
                InProcessFabric(InProcessFabric&&) = delete;
                InProcessFabric& operator=(InProcessFabric&&) = delete;

                /** @brief Returns the number of worker endpoints made on this fabric. */
                [[nodiscard]] std::size_t worker_count() const;

                /**
                 * @brief Sets the policy every later frame is put to; an empty one delivers all.
                 */
                void set_fault_policy(FaultPolicy policy);

                /**
                 * @brief Delivers every held frame.
                 * @param shuffle_seed Seed of the delivery order; 0 keeps the order of arrival.
                 */
                void flush_held(std::uint64_t shuffle_seed = 0);

                /** @brief Returns the number of frames the policy has parked. */
                [[nodiscard]] std::size_t held_count() const;

                /**
                 * @brief Cuts or restores the link of one worker endpoint.
                 *
                 * While cut, frames to and from the worker are discarded and neither side is
                 * told, as on a half-open link; the connection stays open.
                 * @param worker_index Index in the order the worker endpoints were made.
                 */
                void set_partitioned(std::size_t worker_index, bool partitioned);

            private:
                friend class InProcessTransport;

                struct Impl;

                /** @brief Holds the routing state the endpoints share. */
                std::shared_ptr<Impl> impl_;
        };

        /**
         * @brief Holds the settings of a master endpoint on a fabric.
         */
        struct InProcessMasterOptions
        {
            /** @brief Caps the control frames queued per peer; send() then returns QueueFull. */
            std::size_t control_queue_frames = 4096;
        };

        /**
         * @brief Holds the settings of a worker endpoint on a fabric.
         */
        struct InProcessWorkerOptions
        {
            /** @brief Connects again after the master closes the peer or stops. */
            bool reconnect = true;

            /** @brief Holds the wait before a connection attempt is repeated. */
            std::chrono::milliseconds reconnect_delay{1};

            /** @brief Bounds the wait for a started master; 0 waits without a limit. */
            std::chrono::milliseconds connect_timeout{0};

            /** @brief Caps the control frames queued for the master. */
            std::size_t control_queue_frames = 4096;
        };

        /**
         * @brief Attaches one endpoint to an InProcessFabric and keeps the Transport contract.
         *
         * Each endpoint runs one pump thread, which delivers its inbound frames and makes its
         * events. A worker endpoint connects once it and the master endpoint are both started,
         * and each connection is a new PeerID on both sides.
         * @see modules/extension/distributed/README.md
         */
        class SUSHIRUNTIME_API InProcessTransport final : public Transport
        {
            public:
                /**
                 * @brief Creates the master endpoint of a fabric.
                 * @pre The fabric has no master endpoint yet.
                 * @return Never null.
                 */
                [[nodiscard]] static std::shared_ptr<InProcessTransport> make_master(
                    InProcessFabric& fabric,
                    InProcessMasterOptions options = InProcessMasterOptions());

                /**
                 * @brief Creates a worker endpoint; the fabric gives it the next worker index.
                 * @return Never null.
                 */
                [[nodiscard]] static std::shared_ptr<InProcessTransport> make_worker(
                    InProcessFabric& fabric,
                    InProcessWorkerOptions options = InProcessWorkerOptions());

                /** @brief Stops the endpoint and detaches it from the fabric. */
                ~InProcessTransport() override;

                InProcessTransport(const InProcessTransport&) = delete;
                InProcessTransport& operator=(const InProcessTransport&) = delete;
                InProcessTransport(InProcessTransport&&) = delete;
                InProcessTransport& operator=(InProcessTransport&&) = delete;

                /** @brief Starts the pump thread; a worker then connects when the master runs. */
                [[nodiscard]] TransportResult start(TransportEvents& events) override;

                /** @brief Closes the endpoint's connections and joins the pump thread. */
                void stop() noexcept override;

                /** @brief Queues a control frame, which the fault policy sees as it leaves. */
                [[nodiscard]] SendStatus send(PeerID peer, MessageType type,
                                              SushiRuntime::span<const std::byte> payload) override;

                /** @brief Adds a stream the pump pulls one frame at a time. */
                [[nodiscard]] SendStatus send_stream(PeerID peer,
                                                     std::unique_ptr<FrameSource> source) override;

                /** @brief Closes one connection; the other end hears ClosedByPeer. */
                void close_peer(PeerID peer) noexcept override;

                /** @brief Returns Loopback. */
                [[nodiscard]] TransportExposure exposure() const noexcept override;

                /** @brief Returns the payload bytes moved, plus 12 per frame. */
                [[nodiscard]] TransportStatistics statistics() const noexcept override;

                /**
                 * @brief Returns the index the fabric gave this worker endpoint.
                 * @pre This is a worker endpoint.
                 */
                [[nodiscard]] std::size_t worker_index() const noexcept;

            private:
                friend class InProcessFabric;
                friend struct InProcessLink;

                struct Impl;

                /** @brief Wraps the state a factory built. */
                explicit InProcessTransport(std::shared_ptr<Impl> impl) noexcept;

                /** @brief Holds the endpoint's queues; the fabric keeps a weak reference to it. */
                std::shared_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
