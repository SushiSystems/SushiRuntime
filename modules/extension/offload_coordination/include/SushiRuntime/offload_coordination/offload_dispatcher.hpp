/**************************************************************************/
/* offload_dispatcher.hpp                                                 */
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
 * @file offload_dispatcher.hpp
 * @brief Declares OffloadDispatcher, which marks offloadable nodes and dispatches them.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>
#include <SushiRuntime/pending_tasks/pending_tasks.hpp>
#include <SushiRuntime/cluster_load/placement_decision.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>

#include <SushiRuntime/offload_coordination/held_nodes.hpp>
#include <SushiRuntime/offload_coordination/rule_placement.hpp>
#include <SushiRuntime/offload_coordination/worker_directory.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct Node;
        struct OffloadState;
        class OffloadSink;
    }

    namespace Distributed
    {
        /** @brief Holds the dispatches to a worker one node may have in one run. */
        inline constexpr std::uint32_t K_MAXIMUM_OFFLOAD_ATTEMPTS = 2;

        /**
         * @brief Holds the dispatcher's counters since it was built.
         */
        struct OffloadDispatcherStatistics
        {
            std::uint64_t offloaded = 0;       /**< Dispatches queued to a worker. */
            std::uint64_t declined = 0;        /**< try_offload() calls that returned false. */
            std::uint64_t send_queue_full = 0; /**< Dispatches refused by a full queue. */
        };

        /**
         * @brief Translates between graph nodes and the bricks: which node may leave, and how.
         *
         * It is the only brick that reads a node's buffer lists and writes its OffloadState.
         * It owns no mutex and no thread; it calls the registry, the directory, the pending
         * table and the transport one after another, each under that brick's own short lock.
         */
        class SUSHIRUNTIME_API OffloadDispatcher final
        {
            public:
                /**
                 * @brief Builds a dispatcher over bricks that outlive it.
                 * @param master The facts of the machine the master runs on; copied.
                 */
                OffloadDispatcher(BufferRegistry& registry, WorkerDirectory& directory,
                                  PendingTasks& pending, Transport& transport, HeldNodes& held,
                                  NodeFacts master);

                OffloadDispatcher(const OffloadDispatcher&) = delete;
                OffloadDispatcher& operator=(const OffloadDispatcher&) = delete;
                OffloadDispatcher(OffloadDispatcher&&) = delete;
                OffloadDispatcher& operator=(OffloadDispatcher&&) = delete;

                /**
                 * @brief Marks each node a worker may run; the body of prepare() on the seam.
                 *
                 * A node qualifies when its op id is not 0, an alive worker supports it (or its
                 * rule names another machine), every buffer it touches is shared and no two of
                 * its writes name one buffer. Logs one WARN per call that leaves such nodes local.
                 * @param rules Null for none; each node's OffloadState::rules keeps it.
                 */
                void prepare(SushiRuntime::span<const sushi_ptr<Internal::Graph::Node>> nodes,
                             const std::shared_ptr<const PlacementRules>& rules);

                /**
                 * @brief Dispatches a ready node; the body of OffloadCoordinator::try_offload().
                 * In order: decline at the attempt cap; resolve the handles; decide by the
                 * winning rule (RulePlacement), else by WorkerDirectory::pick(); for a worker,
                 * count the attempt, bump the outputs, insert() and send(); for a wait, hold().
                 * @return False when the pool must run the node; true when a worker, a reaper
                 *         or the held table owns it.
                 */
                [[nodiscard]] bool try_offload(Internal::Graph::Node& node);

                /**
                 * @brief Bumps the buffers a locally finished node wrote and clears its attempt
                 *        count; the body of OffloadCoordinator::on_local_completion().
                 *
                 * A node with live lists bumps the addresses in live_writes, not its baked
                 * list. A node without an OffloadState costs one null check.
                 */
                void complete_local(Internal::Graph::Node& node) noexcept;

                /**
                 * @brief Clears the attempt count of a node a worker finished.
                 * @pre The caller owns the node: PendingTasks::take() returned it.
                 */
                void complete_remote(Internal::Graph::Node& node) noexcept;

                /**
                 * @brief Sets the sink a Fail rule ends a run through.
                 * @pre No node is offered yet; the sink outlives the dispatcher's calls.
                 */
                void bind(Internal::Graph::OffloadSink& sink) noexcept;

                /** @brief Returns the counters; reads atomics and takes no lock. */
                [[nodiscard]] OffloadDispatcherStatistics statistics() const noexcept;

            private:
                /** @brief Returns the rule of @p rules that won for a node, or null for none. */
                [[nodiscard]] static const PlacementRule* winning_rule(
                    const Internal::Graph::Node& node, const PlacementRules* rules) noexcept;

                /** @brief Returns where a node goes: by its rule, else by the policy. */
                [[nodiscard]] PlacementDecision decide(const Internal::Graph::Node& node,
                                                       std::uint64_t estimated_bytes);

                /**
                 * @brief Dispatches a node to the worker a decision named.
                 * @pre The handles of the node are in the calling thread's scratch.
                 */
                [[nodiscard]] bool dispatch(Internal::Graph::Node& node,
                                            Internal::Graph::OffloadState& state,
                                            const RunOnWorker& target, std::size_t input_count);

                /**
                 * @brief Hands a node to the held table until a decision's time.
                 * @return None when a release ran since @p releases was read: decide again.
                 */
                [[nodiscard]] std::optional<bool> hold(Internal::Graph::Node& node,
                                                       const WaitForTarget& wait,
                                                       std::uint64_t releases);

                /**
                 * @brief Ends the node's run with the error of a Fail rule, through the sink.
                 * @return True: the sink owns the node; false when no sink is bound.
                 */
                [[nodiscard]] bool refuse(Internal::Graph::Node& node, const FailRun& failure);

                /** @brief Counts one refused dispatch and returns false. */
                [[nodiscard]] bool decline() noexcept;

                /// @brief Names and versions of the shared buffers.
                BufferRegistry& registry_;

                /// @brief Picks the worker and counts its load.
                WorkerDirectory& directory_;

                /// @brief Takes ownership of each dispatched node.
                PendingTasks& pending_;

                /// @brief Carries the dispatch frame.
                Transport& transport_;

                /// @brief Owns the nodes a rule holds for an absent worker.
                HeldNodes& held_;

                /// @brief Ends a run for a Fail rule; null until bind().
                Internal::Graph::OffloadSink* sink_ = nullptr;

                /// @brief Places a node by its winning rule.
                RulePlacement rule_placement_;

                /// @brief Dispatches queued to a worker.
                std::atomic<std::uint64_t> offloaded_{0};

                /// @brief try_offload() calls that returned false.
                std::atomic<std::uint64_t> declined_{0};

                /// @brief Dispatches refused by a full queue.
                std::atomic<std::uint64_t> send_queue_full_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
