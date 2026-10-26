/**************************************************************************/
/* plan_cache.cpp                                                         */
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
 * @file plan_cache.cpp
 * @brief Implements PlanCache, which builds and keeps the plan of one recorder.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 */

#include <SushiRuntime/api/graph/plumbing/plan_cache.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/api/api_log_category.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/engine_handles/plan_access.hpp>
#include "recorder_replay.hpp"
#include "rule_stamp.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Holds the compiled plan of a PlanCache and the nodes of it a recorder named.
         * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
         */
        struct PlanCacheState
        {
            /** @brief Names one node of plan and the rule that recorded it. */
            struct RecordedNode
            {
                const Internal::Graph::Node* node = nullptr; /**< Owned by plan. */
                std::size_t rule = 0; /**< Position of the rule in recording order. */
            };

            /** @brief The compiled plan. */
            Internal::Graph::CompiledGraph plan;

            /** @brief The nodes of plan in recording order; plan lists them in its own. */
            std::vector<RecordedNode> recorded;

            /** @brief The placement record plan was compiled under; its rules may be null. */
            std::shared_ptr<const Internal::Execution::RuntimeContext::PlacementSnapshot>
                placement;
        };

        PlanCache::PlanCache() noexcept = default;

        PlanCache::PlanCache(PlanCache&& other) noexcept
        {
            take(other);
        }

        PlanCache& PlanCache::operator=(PlanCache&& other) noexcept
        {
            if (this != &other)
                take(other);
            return *this;
        }

        PlanCache::~PlanCache() = default;

        Result<void> PlanCache::build(const Recorder& recorder, const ContextHandle& context)
        {
            using Internal::Graph::TaskGraph;

            SR_ZONE("plan.build");

            Internal::Execution::RuntimeContext& runtime =
                Internal::ContextAccess::context(context);
            const auto build_started = std::chrono::steady_clock::now();
            const char* const reason = rebuild_reason(recorder, runtime);
            auto built = std::make_unique<Detail::PlanCacheState>();
            TaskGraph backing_graph;
            std::vector<std::uint32_t> registrations(recorder.rule_count(),
                                                     TaskGraph::K_NO_REGISTRATION);
            built->recorded.reserve(recorder.node_count());
            built->placement = runtime.placement();
            const RuleStamp stamp(built->placement->rules.get(), runtime.machine_facts(),
                                  !built->placement->offload_installed);
            std::optional<Error> refusal;
            Detail::RecorderReplay::replay(
                recorder, backing_graph,
                [&built, &registrations, &backing_graph, &stamp, &refusal](std::size_t rule,
                                                                          std::size_t)
                {
                    Internal::Graph::Node* added = backing_graph.last_node_added();
                    if (added)
                    {
                        Result<void> stamped = stamp.stamp(*added);
                        if (!stamped && !refusal)
                            refusal = std::move(stamped.error());
                        registrations[rule] = added->registration_index;
                        built->recorded.push_back(
                            Detail::PlanCacheState::RecordedNode{added, rule});
                    }
                });
            if (refusal)
                return make_error(std::move(*refusal));
            // Nothing of this build is kept until it compiled; a failure leaves the last one whole.
            Result<Internal::Graph::CompiledGraph> compiled = backing_graph.compile();
            if (!compiled)
                return make_error(std::move(compiled.error()));
            built->plan = std::move(*compiled);
            {
                SR_ZONE_FINE("plan.prepare");
                runtime.execution_engine().prepare(built->plan, built->placement->rules);
                built->plan.profiling = runtime.wants_device_timing();
                built->plan.label = label_;
                built->plan.graph_id = id_.value();
                built->plan.capture_base_state();
            }
            {
                SR_ZONE_FINE("plan.classify");
                built->plan.chain = Internal::Execution::classify_chain(built->plan, runtime);
            }
            state_ = std::move(built);
            registrations_ = std::move(registrations);
            dirty_ = false;
            built_generation_ = recorder.generation();
            ++compiles_;
            SR_LOG_DEBUG(K_API_LOG_CATEGORY, "Plan built ({}): {} nodes in {} ms.", reason,
                         state_->plan.all_nodes.size(),
                         std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - build_started).count());
            return {};
        }

        const char* PlanCache::rebuild_reason(const Recorder& recorder,
                                              Internal::Execution::RuntimeContext& runtime) const noexcept
        {
            if (state_ == nullptr)
                return "first build, or the plan was discarded";
            if (built_generation_ != recorder.generation())
                return "the recorder changed";
            if (state_->placement->generation != runtime.placement()->generation)
                return "the placement changed";
            if (state_->plan.profiling != runtime.wants_device_timing())
                return "the device timing request changed";
            return "the plan was marked for a rebuild";
        }

        bool PlanCache::stale(const Recorder& recorder, const ContextHandle& context) const noexcept
        {
            if (state_ == nullptr || dirty_ || built_generation_ != recorder.generation())
                return true;
            Internal::Execution::RuntimeContext& runtime = Internal::ContextAccess::context(context);
            return state_->placement->generation != runtime.placement()->generation ||
                   state_->plan.profiling != runtime.wants_device_timing();
        }

        void PlanCache::rearm() noexcept
        {
            state_->plan.rearm();
        }

        PlanView PlanCache::describe_plan() const
        {
            PlanView view;
            if (state_ == nullptr)
                return view;
            view.nodes.reserve(state_->recorded.size());
            for (const Detail::PlanCacheState::RecordedNode& recorded : state_->recorded)
            {
                const Internal::Graph::Node& node = *recorded.node;
                PlanNode described;
                described.rule = recorded.rule;
                if (node.metadata.name != nullptr)
                    described.name = node.metadata.name;
                if (node.metadata.origin != nullptr)
                    described.origin = node.metadata.origin;
                described.operation = node.metadata.op_id;
                described.placement_class = node.metadata.placement_class;
                described.placement_key = node.metadata.placement_key;
                described.determinism = node.metadata.determinism;
                described.cost = node.metadata.cost;
                if (node.metadata.placement_rule)
                    described.placement_rule = *node.metadata.placement_rule;
                described.predecessor_count =
                    static_cast<std::size_t>(state_->plan.base_in_degree[node.plan_index]);
                described.reads = node.io_reads_;
                described.writes = node.io_writes_;
                view.nodes.push_back(std::move(described));
            }
            return view;
        }

        Plan* PlanCache::compiled_plan() noexcept
        {
            return surface_plan(&state_->plan);
        }

        const Plan* PlanCache::compiled_plan() const noexcept
        {
            return surface_plan(&state_->plan);
        }

        void PlanCache::discard() noexcept
        {
            state_.reset();
            registrations_.clear();
            built_generation_ = 0;
            dirty_ = true;
        }

        void PlanCache::take(PlanCache& other) noexcept
        {
            state_ = std::move(other.state_);
            registrations_ = std::exchange(other.registrations_, {});
            built_generation_ = std::exchange(other.built_generation_, 0);
            compiles_ = std::exchange(other.compiles_, 0);
            dirty_ = std::exchange(other.dirty_, true);
            label_ = std::exchange(other.label_, {});
            id_ = std::move(other.id_);
        }
    } // namespace Detail
} // namespace SushiRuntime
