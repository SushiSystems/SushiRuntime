/**************************************************************************/
/* offload_dispatcher.cpp                                                 */
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
 * @file offload_dispatcher.cpp
 * @brief Implements OffloadDispatcher: which nodes may leave, and the dispatch of one.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/offload_dispatcher.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/offload_coordination/offload_coordination_log_category.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/wire/wire_codec.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include <SushiRuntime/offload_coordination/offload_handles.hpp>
#include <SushiRuntime/offload_coordination/task_description.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /**
             * @brief Holds what one dispatch builds; each pool thread owns one and reuses it.
             */
            struct DispatchScratch
            {
                /** @brief Holds a dynamic node's live addresses: reads, then writes. */
                std::vector<void*> addresses;

                /** @brief Holds the node's current handles: inputs, then outputs. */
                std::vector<BufferHandle> handles;

                /** @brief Holds the dispatch being encoded; its lists keep their capacity. */
                Wire::TaskDispatchMessage message;

                /** @brief Holds the encoded dispatch. */
                std::vector<std::byte> payload;
            };

            /** @brief Names what prepare() decided about one node. */
            enum class Placement : std::uint8_t
            {
                Untagged,     /**< The node names no operation; it was never a candidate. */
                Offloadable,  /**< A worker may run the node. */
                NoWorker,     /**< No alive worker has a kernel for the operation. */
                NotShared,    /**< A buffer the node touches is not shared. */
                WrittenTwice, /**< Two of the node's writes name one shared buffer. */
            };

            /** @brief Counts the op-tagged nodes one prepare() left local, by reason. */
            struct LocalOnlyCount
            {
                std::size_t tagged = 0;        /**< Nodes that name an operation. */
                std::size_t no_worker = 0;     /**< Of those, left local as NoWorker. */
                std::size_t not_shared = 0;    /**< Of those, left local as NotShared. */
                std::size_t written_twice = 0; /**< Of those, left local as WrittenTwice. */
                std::uint32_t first_op = 0;    /**< The operation of the first one left local. */

                /** @brief Counts one op-tagged node under the placement it got. */
                void count(Placement placement, std::uint32_t operation) noexcept
                {
                    ++tagged;
                    if (placement == Placement::Offloadable)
                        return;

                    if (left_local() == 0)
                        first_op = operation;
                    if (placement == Placement::NoWorker)
                        ++no_worker;
                    else if (placement == Placement::NotShared)
                        ++not_shared;
                    else
                        ++written_twice;
                }

                /** @brief Returns the op-tagged nodes left local, whatever the reason. */
                [[nodiscard]] std::size_t left_local() const noexcept
                {
                    return no_worker + not_shared + written_twice;
                }
            };

            /**
             * @brief Copies what a dispatch says of a task: its operation, its name cut to
             *        what a dispatch carries, and its parameters up to the last one that is
             *        not zero.
             */
            void fill_description(const Internal::Graph::TaskMetadata& metadata,
                                  Wire::TaskDispatchMessage& message)
            {
                static_assert(Wire::K_MAX_DISPATCH_PARAMETERS ==
                                  Internal::Graph::TaskMetadata::K_MAX_PARAMS,
                              "a dispatch carries every parameter a node holds");

                message.operation = metadata.op_id;
                const std::string_view name = metadata.name != nullptr ? metadata.name : "";
                message.name.assign(name.substr(0, Wire::K_MAX_DISPATCH_NAME_BYTES));

                const std::size_t count = used_parameter_count(metadata);
                message.parameter_count = static_cast<std::uint8_t>(count);
                message.parameters.fill(0);
                for (std::size_t index = 0; index < count; ++index)
                    message.parameters[index] = metadata.params[index];
            }

            /** @brief Returns the calling thread's scratch. */
            DispatchScratch& dispatch_scratch()
            {
                thread_local DispatchScratch scratch;
                return scratch;
            }

            /** @brief Returns whether every address of a live list is shared; null is true. */
            bool is_all_shared(const BufferRegistry& registry,
                               const std::shared_ptr<std::vector<void*>>& addresses)
            {
                if (!addresses)
                    return true;

                for (void* address : *addresses)
                    if (!registry.lookup(address).has_value())
                        return false;
                return true;
            }

            /**
             * @brief Returns whether two handles of a list name one buffer.
             *
             * One dispatch raises the version of each output once, so a buffer listed twice
             * would leave with two versions and the fence would refuse one of them.
             */
            bool names_a_buffer_twice(SushiRuntime::span<const BufferHandle> handles) noexcept
            {
                for (std::size_t later = 1; later < handles.size(); ++later)
                    for (std::size_t earlier = 0; earlier < later; ++earlier)
                        if (handles[earlier].id == handles[later].id)
                            return true;
                return false;
            }

            /** @brief Returns whether two addresses of a live write list name one buffer. */
            bool is_written_twice(const BufferRegistry& registry,
                                  const std::shared_ptr<std::vector<void*>>& addresses)
            {
                if (!addresses || addresses->size() < 2)
                    return false;

                std::vector<BufferHandle> handles;
                const SushiRuntime::span<void* const> listed(addresses->data(), addresses->size());
                return registry.resolve(listed, handles) &&
                       names_a_buffer_twice(SushiRuntime::span<const BufferHandle>(
                           handles.data(), handles.size()));
            }

            /**
             * @brief Decides a double-buffered node, whose handles are resolved per dispatch.
             * @param may_wait The node's rule names another machine; no alive worker is needed.
             */
            Placement place_dynamic(const BufferRegistry& registry,
                                    const WorkerDirectory& directory, const Internal::Graph::Node& node,
                                    Internal::Graph::OffloadState& state, bool may_wait)
            {
                state.offloadable = false;
                state.payload.reset();
                state.attempts.store(0, std::memory_order_relaxed);

                const OperationID operation = node.metadata.op_id;
                if (operation.value == 0)
                    return Placement::Untagged;
                if (!may_wait && !directory.supports(operation))
                    return Placement::NoWorker;
                if (!is_all_shared(registry, state.live_reads) ||
                    !is_all_shared(registry, state.live_writes))
                    return Placement::NotShared;
                if (is_written_twice(registry, state.live_writes))
                    return Placement::WrittenTwice;

                state.offloadable = true;
                return Placement::Offloadable;
            }

            /**
             * @brief Decides a fixed-buffer node and stores its handles on it.
             *
             * A node that stays local keeps the handles of the shared buffers it writes; a
             * node that writes none keeps no payload, and gets no OffloadState for it.
             * @param may_wait The node's rule names another machine; no alive worker is needed.
             */
            Placement place_fixed(const BufferRegistry& registry, const WorkerDirectory& directory,
                                  Internal::Graph::Node& node, bool may_wait)
            {
                const OperationID operation = node.metadata.op_id;
                const bool is_tagged = operation.value != 0;
                const bool is_supported =
                    is_tagged && (may_wait || directory.supports(operation));

                auto baked = std::make_unique<OffloadHandles>();
                bool every_buffer_shared = true;
                if (is_supported)
                {
                    baked->handles.reserve(node.io_reads_.size() + node.io_writes_.size());
                    for (const BufferRegion& region : node.io_reads_)
                    {
                        const std::optional<BufferHandle> handle = registry.lookup(region.base);
                        if (!handle.has_value())
                        {
                            every_buffer_shared = false;
                            break;
                        }
                        baked->handles.push_back(*handle);
                    }
                }

                if (!every_buffer_shared)
                    baked->handles.clear();
                baked->input_count = baked->handles.size();

                for (const BufferRegion& region : node.io_writes_)
                {
                    const std::optional<BufferHandle> handle = registry.lookup(region.base);
                    if (handle.has_value())
                        baked->handles.push_back(*handle);
                    else
                        every_buffer_shared = false;
                }

                const bool has_repeated_write =
                    names_a_buffer_twice(SushiRuntime::span<const BufferHandle>(
                        baked->handles.data() + baked->input_count,
                        baked->handles.size() - baked->input_count));
                const bool is_offloadable =
                    is_supported && every_buffer_shared && !has_repeated_write;
                if (!is_offloadable)
                {
                    // A local node keeps only what complete_local() bumps: its shared writes.
                    baked->handles.erase(
                        baked->handles.begin(),
                        baked->handles.begin() + static_cast<std::ptrdiff_t>(baked->input_count));
                    baked->input_count = 0;
                }

                if (is_offloadable || !baked->handles.empty())
                {
                    Internal::Graph::OffloadState& state = node.offload();
                    state.payload = std::move(baked);
                    state.offloadable = is_offloadable;
                    state.attempts.store(0, std::memory_order_relaxed);
                }
                else if (Internal::Graph::OffloadState* state = node.offload_state())
                {
                    state->offloadable = false;
                    state->payload.reset();
                    state->attempts.store(0, std::memory_order_relaxed);
                }

                if (!is_tagged)
                    return Placement::Untagged;
                if (is_offloadable)
                    return Placement::Offloadable;
                if (!is_supported)
                    return Placement::NoWorker;
                return every_buffer_shared ? Placement::WrittenTwice : Placement::NotShared;
            }

            /**
             * @brief Writes a node's current handles into the scratch under one registry lock.
             *
             * A double-buffered node resolves the addresses of its live lists; a fixed-buffer
             * node refreshes the versions of the handles prepare() stored.
             * @return The number of leading handles that are inputs, or nothing when a buffer
             *         is not shared any more.
             */
            std::optional<std::size_t> snapshot_handles(const BufferRegistry& registry,
                                                        const Internal::Graph::OffloadState& state,
                                                        DispatchScratch& scratch)
            {
                if (state.live_reads || state.live_writes)
                {
                    scratch.addresses.clear();
                    if (state.live_reads)
                        scratch.addresses.insert(scratch.addresses.end(),
                                                 state.live_reads->begin(),
                                                 state.live_reads->end());
                    const std::size_t input_count = scratch.addresses.size();
                    if (state.live_writes)
                        scratch.addresses.insert(scratch.addresses.end(),
                                                 state.live_writes->begin(),
                                                 state.live_writes->end());

                    const SushiRuntime::span<void* const> addresses(scratch.addresses.data(),
                                                                    scratch.addresses.size());
                    if (!registry.resolve(addresses, scratch.handles))
                        return std::nullopt;
                    return input_count;
                }

                const auto* baked = static_cast<const OffloadHandles*>(state.payload.get());
                if (baked == nullptr)
                    return std::nullopt;

                scratch.handles.assign(baked->handles.begin(), baked->handles.end());
                const SushiRuntime::span<BufferHandle> handles(scratch.handles.data(),
                                                               scratch.handles.size());
                if (!registry.refresh(handles))
                    return std::nullopt;
                return baked->input_count;
            }
        } // namespace

        OffloadDispatcher::OffloadDispatcher(BufferRegistry& registry, WorkerDirectory& directory,
                                             PendingTasks& pending, Transport& transport,
                                             HeldNodes& held, NodeFacts master)
            : registry_(registry),
              directory_(directory),
              pending_(pending),
              transport_(transport),
              held_(held),
              rule_placement_(directory, held, std::move(master))
        {

        }

        void OffloadDispatcher::prepare(SushiRuntime::span<const sushi_ptr<Internal::Graph::Node>> nodes,
                                        const std::shared_ptr<const PlacementRules>& rules)
        {
            LocalOnlyCount local;
            for (const sushi_ptr<Internal::Graph::Node>& slot : nodes)
            {
                Internal::Graph::Node* node = slot.get();
                if (node == nullptr)
                    continue;

                Internal::Graph::OffloadState* state = node->offload_state();
                const bool is_dynamic =
                    state != nullptr && (state->live_reads || state->live_writes);
                const PlacementRule* rule = winning_rule(*node, rules.get());
                const bool may_wait = rule != nullptr && !rule_placement_.names_master(*rule);
                const Placement placement =
                    is_dynamic ? place_dynamic(registry_, directory_, *node, *state, may_wait)
                               : place_fixed(registry_, directory_, *node, may_wait);
                if (Internal::Graph::OffloadState* placed = node->offload_state())
                    placed->rules = rules;
                if (placement == Placement::Untagged)
                    continue;

                local.count(placement, node->metadata.op_id.value);
            }

            if (local.left_local() != 0)
                SR_LOG_WARN(K_OFFLOAD_COORDINATION_LOG_CATEGORY, "{} of {} op-tagged nodes will run locally: {} no live worker for "
                            "the op, {} buffer not shared, {} buffer written twice (first: op "
                            "{})",
                            local.left_local(), local.tagged, local.no_worker, local.not_shared,
                            local.written_twice, local.first_op);
        }

        bool OffloadDispatcher::try_offload(Internal::Graph::Node& node)
        {
            Internal::Graph::OffloadState* state = node.offload_state();
            if (state == nullptr)
                return decline();

            // A late-bound node disabled for the step completes at once in the pool.
            if (node.bindings_ && !node.bindings_->enabled.load(std::memory_order_relaxed))
                return decline();

            if (state->attempts.load(std::memory_order_relaxed) >= K_MAXIMUM_OFFLOAD_ATTEMPTS)
                return decline();

            DispatchScratch& scratch = dispatch_scratch();
            const std::optional<std::size_t> input_count =
                snapshot_handles(registry_, *state, scratch);
            if (!input_count.has_value())
                return decline();

            std::uint64_t estimated_bytes = 0;
            for (std::size_t index = 0; index < *input_count; ++index)
                estimated_bytes += scratch.handles[index].size_bytes;

            // A release between the decision and the hold would be missed; decide again then.
            for (;;)
            {
                const std::uint64_t releases = held_.releases();
                const PlacementDecision decision = decide(node, estimated_bytes);
                const std::optional<bool> taken = std::visit(
                    [&](const auto& outcome) -> std::optional<bool>
                    {
                        using Outcome = std::decay_t<decltype(outcome)>;
                        if constexpr (std::is_same_v<Outcome, RunOnWorker>)
                            return dispatch(node, *state, outcome, *input_count);
                        else if constexpr (std::is_same_v<Outcome, WaitForTarget>)
                            return hold(node, outcome, releases);
                        else if constexpr (std::is_same_v<Outcome, FailRun>)
                            return refuse(node, outcome);
                        else
                            return decline();
                    },
                    decision);
                if (taken.has_value())
                    return *taken;
            }
        }

        const PlacementRule* OffloadDispatcher::winning_rule(
            const Internal::Graph::Node& node, const PlacementRules* rules) noexcept
        {
            const std::optional<std::uint32_t> position = node.metadata.placement_rule;
            if (rules == nullptr || !position.has_value() || *position >= rules->size())
                return nullptr;
            return &rules->rule(*position);
        }

        PlacementDecision OffloadDispatcher::decide(const Internal::Graph::Node& node,
                                                    std::uint64_t estimated_bytes)
        {
            const Internal::Graph::OffloadState* state = node.offload_state();
            const PlacementRule* rule =
                winning_rule(node, state != nullptr ? state->rules.get() : nullptr);
            if (rule == nullptr)
            {
                const std::optional<PeerID> worker = directory_.pick(node, estimated_bytes);
                if (!worker.has_value())
                    return RunLocally{};
                return RunOnWorker{static_cast<int>(worker->value), 0, std::nullopt};
            }

            const PlacementDecision decision = rule_placement_.decide(
                *rule, *node.metadata.placement_rule, node, std::chrono::steady_clock::now());
            if (!std::holds_alternative<WaitForTarget>(decision))
                held_.forget(node);
            return decision;
        }

        bool OffloadDispatcher::dispatch(Internal::Graph::Node& node,
                                         Internal::Graph::OffloadState& state,
                                         const RunOnWorker& target, std::size_t input_count)
        {
            const PeerID worker{static_cast<std::uint32_t>(target.worker_id)};

            // The attempt is the last write to the node before the pending table may own it.
            state.attempts.fetch_add(1, std::memory_order_relaxed);

            DispatchScratch& scratch = dispatch_scratch();
            const SushiRuntime::span<BufferHandle> outputs(
                scratch.handles.data() + input_count, scratch.handles.size() - input_count);
            registry_.bump(outputs);

            const std::uint64_t task_id = pending_.allocate_task_id();
            const auto first_output =
                scratch.handles.begin() + static_cast<std::ptrdiff_t>(input_count);
            scratch.message.task_id = task_id;
            scratch.message.device_index = static_cast<std::uint32_t>(target.device_index);
            fill_description(node.metadata, scratch.message);
            scratch.message.inputs.assign(scratch.handles.begin(), first_output);
            scratch.message.outputs.assign(first_output, scratch.handles.end());

            scratch.payload.clear();
            const bool is_encoded =
                scratch.message.encode(scratch.payload) == Wire::CodecStatus::Ok;
            const bool is_inserted =
                is_encoded &&
                pending_.insert(task_id, node, worker,
                                SushiRuntime::span<const BufferHandle>(outputs.data(),
                                                                       outputs.size()),
                                std::chrono::steady_clock::now(), target.deadline);
            if (!is_inserted)
            {
                directory_.release_inflight(worker);
                return decline();
            }

            // The pending table owns the node from here; this thread reads and writes it no more.
            const SendStatus sent = transport_.send(
                worker, MessageType::TaskDispatch,
                SushiRuntime::span<const std::byte>(scratch.payload.data(),
                                                    scratch.payload.size()));
            if (sent == SendStatus::Queued)
            {
                offloaded_.fetch_add(1, std::memory_order_relaxed);
                SR_LOG_DEBUG(K_OFFLOAD_COORDINATION_LOG_CATEGORY,
                             "task {} queued for worker {} device {}", task_id, worker.value,
                             target.device_index);
                return true;
            }

            if (sent == SendStatus::QueueFull)
                send_queue_full_.fetch_add(1, std::memory_order_relaxed);

            // A removal that got there first owns the node and resubmits it.
            const std::optional<PendingTask> undone = pending_.take(task_id, worker);
            if (!undone.has_value())
            {
                offloaded_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            directory_.release_inflight(worker);
            return decline();
        }

        std::optional<bool> OffloadDispatcher::hold(Internal::Graph::Node& node,
                                                    const WaitForTarget& wait,
                                                    std::uint64_t releases)
        {
            const HoldStatus status = held_.hold(node, wait.until, releases);
            if (status == HoldStatus::Held)
                return true;
            if (status == HoldStatus::Stale)
                return std::nullopt;

            held_.forget(node);
            return decline();
        }

        bool OffloadDispatcher::refuse(Internal::Graph::Node& node, const FailRun& failure)
        {
            if (sink_ == nullptr)
            {
                SR_LOG_ERROR(K_OFFLOAD_COORDINATION_LOG_CATEGORY,
                             "{} No sink is bound, so the node runs on the master.",
                             failure.error.message);
                return decline();
            }

            // The sink owns the node from here and ends its run.
            sink_->fail(node, failure.error);
            return true;
        }

        void OffloadDispatcher::bind(Internal::Graph::OffloadSink& sink) noexcept
        {
            sink_ = &sink;
        }

        void OffloadDispatcher::complete_local(Internal::Graph::Node& node) noexcept
        {
            Internal::Graph::OffloadState* state = node.offload_state();
            if (state == nullptr)
                return;

            state->attempts.store(0, std::memory_order_relaxed);

            if (state->live_writes)
            {
                registry_.bump_addresses(SushiRuntime::span<void* const>(
                    state->live_writes->data(), state->live_writes->size()));
                return;
            }

            auto* baked = static_cast<OffloadHandles*>(state->payload.get());
            if (baked == nullptr)
                return;

            const SushiRuntime::span<BufferHandle> written = baked->outputs();
            if (!written.empty())
                registry_.bump(written);
        }

        void OffloadDispatcher::complete_remote(Internal::Graph::Node& node) noexcept
        {
            if (Internal::Graph::OffloadState* state = node.offload_state())
                state->attempts.store(0, std::memory_order_relaxed);
        }

        OffloadDispatcherStatistics OffloadDispatcher::statistics() const noexcept
        {
            OffloadDispatcherStatistics snapshot;
            snapshot.offloaded = offloaded_.load(std::memory_order_relaxed);
            snapshot.declined = declined_.load(std::memory_order_relaxed);
            snapshot.send_queue_full = send_queue_full_.load(std::memory_order_relaxed);
            return snapshot;
        }

        bool OffloadDispatcher::decline() noexcept
        {
            declined_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

    } // namespace Distributed
} // namespace SushiRuntime
