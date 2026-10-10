/**************************************************************************/
/* worker_agent.cpp                                                       */
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
 * @file worker_agent.cpp
 * @brief Implements WorkerAgent: the four worker bricks composed over one transport.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/worker_agent.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/version.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>

#include "dispatch_deduplicator.hpp"
#include "dispatch_executor.hpp"
#include "handshake.hpp"
#include "master_link.hpp"
#include "message_router.hpp"
#include "wire_codec.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"
#include "worker_buffer_cache.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the device the worker's kernels run on and its cache lives on. */
            constexpr std::size_t K_DEVICE_INDEX = 0;

            /** @brief Holds the cache budget of a device that reports no memory size. */
            constexpr std::uint64_t K_FALLBACK_CACHE_BYTES = 1024ull * 1024ull * 1024ull;

            /** @brief Holds the longest name HELLO can carry, in bytes. */
            constexpr std::size_t K_MAXIMUM_NAME_BYTES = 255;

            /** @brief Bounds the wait for the master's connection to drain in stop(). */
            constexpr std::chrono::milliseconds K_CLOSE_LIMIT{2000};

            /** @brief Names the payload of a frame as a handler receives it. */
            using Payload = SushiRuntime::span<const std::byte>;

            /** @brief Refuses a constructor argument the caller must not pass. */
            [[noreturn]] void refuse(const char* message)
            {
                throw Core::SushiException(Core::Error{Core::Errc::invalid_access, message});
            }

            /** @brief Returns the transport, or refuses a null one. */
            std::shared_ptr<ITransport> require_transport(std::shared_ptr<ITransport> transport)
            {
                if (!transport)
                    refuse("WorkerAgent needs a transport; a null one was passed");
                return transport;
            }

            /** @brief Returns the registry, or refuses a null one. */
            sushi_ptr<KernelRegistry> require_kernels(sushi_ptr<KernelRegistry> kernels)
            {
                if (!kernels)
                    refuse("WorkerAgent needs a kernel registry; a null one was passed");
                return kernels;
            }

            /** @brief Returns the options with a name HELLO can carry. */
            WorkerAgentOptions bounded(WorkerAgentOptions options)
            {
                if (options.name.size() > K_MAXIMUM_NAME_BYTES)
                {
                    SR_LOG_WARN("worker name holds {} bytes; HELLO carries {}, so the name is cut "
                                "to that length",
                                options.name.size(), K_MAXIMUM_NAME_BYTES);
                    options.name.resize(K_MAXIMUM_NAME_BYTES);
                }
                return options;
            }

            /** @brief Returns the cache budget: the one asked for, or half the device's memory. */
            std::uint64_t resolve_cache_budget(Execution::RuntimeContext& context,
                                               std::uint64_t requested)
            {
                if (requested != 0)
                    return requested;

                const std::vector<Device::DeviceInfo>& devices = context.devices().devices();
                const std::uint64_t half = devices.size() > K_DEVICE_INDEX
                                               ? devices[K_DEVICE_INDEX].memory_bytes / 2
                                               : 0;
                if (half != 0)
                    return half;

                SR_LOG_WARN("device {} reports no memory size; the worker's buffer cache takes "
                            "{} bytes",
                            K_DEVICE_INDEX, K_FALLBACK_CACHE_BYTES);
                return K_FALLBACK_CACHE_BYTES;
            }

            /** @brief Returns an allocator that keeps cached buffers as recorded shared USM. */
            CacheAllocator make_cache_allocator(Memory::UsmAllocator& memory)
            {
                CacheAllocator allocator;
                allocator.allocate = [&memory](std::size_t size_bytes) -> void*
                {
                    try
                    {
                        return memory.allocate(Memory::UsmRequest{Memory::UsmKind::Shared,
                                                                  size_bytes, 0, K_DEVICE_INDEX});
                    }
                    catch (const std::exception& error)
                    {
                        SR_LOG_ERROR("cannot allocate {} bytes of worker cache on device {}: {}; "
                                     "the task that needs them fails",
                                     size_bytes, K_DEVICE_INDEX, error.what());
                        return nullptr;
                    }
                    catch (...)
                    {
                        SR_LOG_ERROR("cannot allocate {} bytes of worker cache on device {}: "
                                     "unknown exception; the task that needs them fails",
                                     size_bytes, K_DEVICE_INDEX);
                        return nullptr;
                    }
                };
                allocator.release = [&memory](void* address) { memory.release(address); };
                return allocator;
            }

            /** @brief Returns the device's free memory, its total when no query exists, else 0. */
            std::uint64_t read_free_memory(sycl::queue& queue)
            {
                try
                {
                    const sycl::device device = queue.get_device();
#if defined(SYCL_EXT_INTEL_DEVICE_INFO) && SYCL_EXT_INTEL_DEVICE_INFO >= 5
                    if (device.has(sycl::aspect::ext_intel_free_memory))
                        return device.get_info<sycl::ext::intel::info::device::free_memory>();
#endif
                    return device.get_info<sycl::info::device::global_mem_size>();
                }
                catch (const std::exception& error)
                {
                    SR_LOG_DEBUG("cannot read the free memory of device {}: {}; PONG carries 0",
                                 K_DEVICE_INDEX, error.what());
                    return 0;
                }
            }
        } // namespace

        /**
         * @brief Holds the worker's bricks and receives the transport's events for them.
         */
        struct WorkerAgent::Impl final : public TransportEvents
        {
            public:
                /** @brief Builds the bricks and the routing table; no thread runs yet. */
                Impl(Execution::RuntimeContext& context, std::shared_ptr<ITransport> transport,
                     sushi_ptr<KernelRegistry> kernels, WorkerAgentOptions options);

                /** @brief Starts the exec thread, the heartbeat thread and the transport. */
                [[nodiscard]] TransportResult start();

                /** @brief Stops the bricks and the transport in the order the README gives. */
                void stop() noexcept;

                /** @brief Returns the link's end, waiting for it at most the timeout. */
                [[nodiscard]] WorkerEnd wait_for_end(std::chrono::milliseconds timeout);

                /** @brief Returns the counters of the executor, the cache and the link. */
                [[nodiscard]] WorkerStatistics statistics() const noexcept;

                /** @brief Records the connection and opens the handshake on it. */
                void on_peer_connected(PeerId peer, const PeerInfo& info) override;

                /** @brief Refreshes the master watch, then routes the frame to its brick. */
                void on_frame(PeerId peer, MessageType type, std::uint8_t flags,
                              Payload payload) override;

                /** @brief Forgets the connection and acts on the link's answer. */
                void on_peer_lost(PeerId peer, const PeerLoss& loss) override;

                /** @brief Ends the link and logs the transport's cause when that ended it. */
                void on_connect_abandoned(const std::string& cause) override;

            private:
                /** @brief Names how far the agent's life has come. */
                enum class Phase : std::uint8_t
                {
                    Built,   /**< start() has not been called. */
                    Started, /**< start() ran; stop() has work to do. */
                    Stopped, /**< stop() ran, or start() failed. */
                };

                /** @brief Names the routing table: peer, flag byte and payload per frame. */
                using Router = Wire::MessageRouter<PeerId, std::uint8_t, Payload>;

                /** @brief Returns what the link tells the master and how it probes this worker. */
                [[nodiscard]] MasterLinkOptions make_link_options(const KernelRegistry& kernels);

                /**
                 * @brief Routes one message type to a handler that receives it decoded.
                 * @tparam Handle Callable as `void(PeerId, std::uint8_t flags, Message&)`.
                 */
                template <typename Message, typename Handle>
                void route(Handle handle);

                /** @brief Fills the routing table, one row per message a worker receives. */
                void build_routes();

                /** @brief Logs a frame that did not decode and closes its connection. */
                void refuse_malformed(PeerId peer, MessageType type, std::size_t length,
                                      const Wire::DecodeResult& decoded);

                /** @brief Acts on the other bricks as a link change asks. */
                void apply(LinkChange change);

                /** @brief Queues a dispatch from the registered master unless it was seen. */
                void accept_dispatch(PeerId peer, Wire::TaskDispatchMessage& dispatch);

                /** @brief Returns the endpoint of a connection for a log line. */
                [[nodiscard]] std::string endpoint_of(PeerId peer) const;

                /** @brief Closes the registered connection and waits until its queue drained. */
                void close_master() noexcept;

                /** @brief Writes the counters as one INFO line. */
                void log_statistics() const noexcept;

                /// @brief Supplies the queue kernels run on and the cache's memory.
                Execution::RuntimeContext& context_;

                /// @brief Carries every frame; the agent is its only receiver.
                std::shared_ptr<ITransport> transport_;

                /// @brief The settings; owns the secret the link borrows.
                WorkerAgentOptions options_;

                /// @brief Guards connected_ and endpoint_.
                mutable std::mutex mutex_;

                /// @brief Wakes close_master() when a connection ends.
                std::condition_variable connection_changed_;

                /// @brief The connection that is open; 0 for none.
                PeerId connected_;

                /// @brief The master's address and port on that connection.
                std::string endpoint_;

                /// @brief How far the agent's life has come.
                std::atomic<Phase> phase_{Phase::Built};

                /// @brief The buffers of the tasks; destroyed after the transport stopped.
                WorkerBufferCache cache_;

                /// @brief The task ids seen in the master's session.
                DispatchDeduplicator deduplicator_;

                /// @brief The handshake, the heartbeat and the master watch.
                MasterLink link_;

                /// @brief The job queue and the exec thread.
                DispatchExecutor executor_;

                /// @brief Maps each message type to the brick that owns it.
                Router router_;
        };

        WorkerAgent::Impl::Impl(Execution::RuntimeContext& context,
                                std::shared_ptr<ITransport> transport,
                                sushi_ptr<KernelRegistry> kernels, WorkerAgentOptions options)
            : context_(context), transport_(require_transport(std::move(transport))),
              options_(bounded(std::move(options))),
              cache_(make_cache_allocator(context.memory()),
                     resolve_cache_budget(context, options_.cache_bytes)),
              link_(*transport_, options_.secret.bytes(),
                    make_link_options(*require_kernels(kernels)), make_nonce),
              executor_(kernels, cache_, *transport_, context.devices().queue(K_DEVICE_INDEX),
                        DispatchExecutorOptions{options_.max_jobs, options_.pull_idle_timeout})
        {
            build_routes();
        }

        MasterLinkOptions WorkerAgent::Impl::make_link_options(const KernelRegistry& kernels)
        {
            MasterLinkOptions link;
            link.name = options_.name;
            link.build_id = runtime_version_string();
            link.ops = kernels.known_ops();
            link.max_jobs = static_cast<std::uint32_t>(std::min<std::size_t>(
                options_.max_jobs, std::numeric_limits<std::uint32_t>::max()));
            link.cache_budget = cache_.budget();
            link.heartbeat_interval = options_.heartbeat_interval;
            link.heartbeat_probe = [this]
            {
                Wire::HeartbeatMessage heartbeat;
                heartbeat.queue_depth = executor_.queue_depth();
                heartbeat.oldest_held_task_id = executor_.oldest_held_task_id();
                heartbeat.cache_bytes = cache_.bytes();
                return heartbeat;
            };
            link.free_memory_probe = [this]
            {
                return read_free_memory(context_.devices().queue(K_DEVICE_INDEX));
            };
            return link;
        }

        template <typename Message, typename Handle>
        void WorkerAgent::Impl::route(Handle handle)
        {
            router_.set_handler(
                Message::K_TYPE,
                [this, handle = std::move(handle)](PeerId peer, std::uint8_t flags,
                                                   Payload payload)
                {
                    Message message;
                    const Wire::DecodeResult decoded = Message::decode(payload, message);
                    if (!decoded.ok())
                    {
                        refuse_malformed(peer, Message::K_TYPE, payload.size(), decoded);
                        return;
                    }
                    handle(peer, flags, message);
                });
        }

        void WorkerAgent::Impl::build_routes()
        {
            route<Wire::ChallengeMessage>(
                [this](PeerId peer, std::uint8_t, Wire::ChallengeMessage& challenge)
                {
                    apply(link_.on_challenge(peer, challenge));
                });
            route<Wire::WelcomeMessage>(
                [this](PeerId peer, std::uint8_t, Wire::WelcomeMessage& welcome)
                {
                    apply(link_.on_welcome(peer, welcome));
                });
            route<Wire::RejectMessage>(
                [this](PeerId peer, std::uint8_t, Wire::RejectMessage& reject)
                {
                    apply(link_.on_reject(peer, reject));
                });
            route<Wire::GoodbyeMessage>(
                [this](PeerId peer, std::uint8_t, Wire::GoodbyeMessage& goodbye)
                {
                    link_.on_goodbye(peer, goodbye);
                });
            route<Wire::PingMessage>(
                [this](PeerId peer, std::uint8_t, Wire::PingMessage& ping)
                {
                    link_.on_ping(peer, ping);
                });
            route<Wire::TaskDispatchMessage>(
                [this](PeerId peer, std::uint8_t, Wire::TaskDispatchMessage& dispatch)
                {
                    accept_dispatch(peer, dispatch);
                });
            route<Wire::BufferChunkMessage>(
                [this](PeerId, std::uint8_t flags, Wire::BufferChunkMessage& chunk)
                {
                    if (chunk.purpose == Wire::ChunkPurpose::PullReply)
                        cache_.accept_chunk(chunk, (flags & Wire::K_FRAME_FLAG_LAST_CHUNK) != 0);
                });
            route<Wire::BufferAbortMessage>(
                [this](PeerId, std::uint8_t, Wire::BufferAbortMessage& abort)
                {
                    cache_.abort_transfer(abort.id, abort.version);
                });
            route<Wire::BufferReleaseMessage>(
                [this](PeerId, std::uint8_t, Wire::BufferReleaseMessage& release)
                {
                    cache_.release(release.id);
                });
        }

        void WorkerAgent::Impl::refuse_malformed(PeerId peer, MessageType type,
                                                 std::size_t length,
                                                 const Wire::DecodeResult& decoded)
        {
            SR_LOG_WARN("master {} sent a malformed {} frame ({} bytes): the field at offset {} "
                        "did not decode (codec status {}); closing the connection",
                        endpoint_of(peer), message_type_name(type), length, decoded.offset,
                        static_cast<int>(decoded.status));
            transport_->close_peer(peer);
        }

        void WorkerAgent::Impl::apply(LinkChange change)
        {
            if (change == LinkChange::RegisteredNewSession)
            {
                cache_.clear();
                deduplicator_.clear();
                return;
            }

            if (change == LinkChange::Disconnected)
                executor_.discard_queued();
        }

        void WorkerAgent::Impl::accept_dispatch(PeerId peer,
                                                Wire::TaskDispatchMessage& dispatch)
        {
            const std::uint64_t task_id = dispatch.task_id;
            const std::optional<PeerId> master = link_.master();
            if (!master || *master != peer)
            {
                SR_LOG_DEBUG("task {}: dispatched on peer {}, which is not registered; ignored",
                             task_id, peer.value);
                return;
            }

            if (!deduplicator_.mark_seen(task_id))
                return;

            const EnqueueStatus status =
                executor_.enqueue(peer, std::move(dispatch), link_.chunk_bytes());
            if (status == EnqueueStatus::QueueFull)
                SR_LOG_DEBUG("task {}: the job queue holds {} tasks, its bound; nothing is sent "
                             "and the master takes the task back",
                             task_id, options_.max_jobs);
        }

        std::string WorkerAgent::Impl::endpoint_of(PeerId peer) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (connected_ == peer && !endpoint_.empty())
                return endpoint_;
            return "(peer " + std::to_string(peer.value) + ")";
        }

        TransportResult WorkerAgent::Impl::start()
        {
            Phase expected = Phase::Built;
            if (!phase_.compare_exchange_strong(expected, Phase::Started))
                return TransportResult{TransportStatus::AlreadyStarted,
                                       "the worker agent was started before"};

            TransportResult result;
            try
            {
                executor_.start();
                link_.start();
                result = transport_->start(*this);
            }
            catch (const std::exception& error)
            {
                result = TransportResult{TransportStatus::Failed,
                                         std::string("the worker agent could not start: ") +
                                             error.what()};
            }

            if (!result.is_ok())
            {
                phase_.store(Phase::Stopped);
                executor_.stop();
                transport_->stop();
                link_.stop();
                SR_LOG_ERROR("worker agent did not start: {}; no thread is left running",
                             result.message);
            }
            return result;
        }

        void WorkerAgent::Impl::close_master() noexcept
        {
            try
            {
                const std::optional<PeerId> master = link_.master();
                if (!master)
                    return;

                transport_->close_peer(*master);
                std::unique_lock<std::mutex> lock(mutex_);
                const bool closed = connection_changed_.wait_for(
                    lock, K_CLOSE_LIMIT, [&] { return connected_ != *master; });
                if (!closed)
                    SR_LOG_WARN("master {} did not take the worker's last frames within {} ms; "
                                "GOODBYE may be lost and the master then waits for its liveness "
                                "timeout",
                                endpoint_, K_CLOSE_LIMIT.count());
            }
            catch (const std::exception& error)
            {
                SR_LOG_WARN("could not close the master's connection in order: {}; the "
                            "transport's stop closes it",
                            error.what());
            }
        }

        void WorkerAgent::Impl::log_statistics() const noexcept
        {
            try
            {
                const WorkerStatistics counters = statistics();
                SR_LOG_INFO("worker stopped: {} tasks run; failed: {} kernel, {} output "
                            "allocation, {} input unavailable, {} unknown operation, {} shutting "
                            "down; cache {} bytes, {} evictions; {} reconnects",
                            counters.tasks_run, counters.tasks_failed_kernel,
                            counters.tasks_failed_output_allocation,
                            counters.tasks_failed_input_unavailable,
                            counters.tasks_failed_unknown_operation,
                            counters.tasks_failed_shutting_down, counters.cache_bytes,
                            counters.cache_evictions, counters.reconnects);
            }
            catch (const std::exception& error)
            {
                SR_LOG_WARN("could not log the worker's statistics: {}", error.what());
            }
        }

        void WorkerAgent::Impl::stop() noexcept
        {
            Phase expected = Phase::Started;
            if (!phase_.compare_exchange_strong(expected, Phase::Stopped))
            {
                expected = Phase::Built;
                (void)phase_.compare_exchange_strong(expected, Phase::Stopped);
                return;
            }

            executor_.stop();
            link_.say_goodbye();
            close_master();
            transport_->stop();
            link_.stop();
            log_statistics();
        }

        WorkerEnd WorkerAgent::Impl::wait_for_end(std::chrono::milliseconds timeout)
        {
            return link_.wait_for_end(timeout);
        }

        WorkerStatistics WorkerAgent::Impl::statistics() const noexcept
        {
            WorkerStatistics counters = executor_.statistics();
            counters.cache_bytes = cache_.bytes();
            counters.cache_evictions = cache_.evictions();
            counters.reconnects = link_.reconnects();
            return counters;
        }

        void WorkerAgent::Impl::on_peer_connected(PeerId peer, const PeerInfo& info)
        {
            if (phase_.load() == Phase::Stopped)
            {
                transport_->close_peer(peer);
                return;
            }

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                connected_ = peer;
                endpoint_ = info.endpoint;
            }

            SR_LOG_INFO("connected to master {} (peer {})", info.endpoint, peer.value);
            link_.on_connected(peer);
        }

        void WorkerAgent::Impl::on_frame(PeerId peer, MessageType type, std::uint8_t flags,
                                         Payload payload)
        {
            link_.note_frame(peer);
            if (router_.route(type, peer, flags, payload) == Wire::RouteStatus::Unhandled)
                SR_LOG_DEBUG("master {} sent a {} frame, which a worker does not take; ignored",
                             endpoint_of(peer), message_type_name(type));
        }

        void WorkerAgent::Impl::on_peer_lost(PeerId peer, const PeerLoss& loss)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (connected_ == peer)
                    connected_ = PeerId{};
            }
            connection_changed_.notify_all();

            apply(link_.on_lost(peer, loss));
        }

        void WorkerAgent::Impl::on_connect_abandoned(const std::string& cause)
        {
            const LinkChange change = link_.on_connect_abandoned();
            if (change == LinkChange::Ended)
                SR_LOG_WARN("the transport gave up on the master: {}; this worker stops serving",
                            cause);
            else
                SR_LOG_DEBUG("the transport gave up on the master while this worker stops: {}",
                             cause);
            apply(change);
        }

        WorkerAgent::WorkerAgent(API::Runtime& runtime, std::shared_ptr<ITransport> transport,
                                 sushi_ptr<KernelRegistry> kernels, WorkerAgentOptions options)
            : WorkerAgent(runtime.unsafe_context(), std::move(transport), std::move(kernels),
                          std::move(options))
        {

        }

        WorkerAgent::WorkerAgent(Execution::RuntimeContext& context,
                                 std::shared_ptr<ITransport> transport,
                                 sushi_ptr<KernelRegistry> kernels, WorkerAgentOptions options)
            : impl_(std::make_unique<Impl>(context, std::move(transport), std::move(kernels),
                                           std::move(options)))
        {

        }

        WorkerAgent::~WorkerAgent()
        {
            impl_->stop();
        }

        TransportResult WorkerAgent::start()
        {
            return impl_->start();
        }

        WorkerEnd WorkerAgent::wait_for_end(std::chrono::milliseconds timeout)
        {
            return impl_->wait_for_end(timeout);
        }

        void WorkerAgent::stop() noexcept
        {
            impl_->stop();
        }

        WorkerStatistics WorkerAgent::statistics() const noexcept
        {
            return impl_->statistics();
        }

    } // namespace Distributed
} // namespace SushiRuntime
