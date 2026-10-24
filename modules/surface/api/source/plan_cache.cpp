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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
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

        PlanCache::PlanCache(bool profiling) noexcept : profiling_requested_(profiling)
        {
        }

        PlanCache::PlanCache(PlanCache&& other) noexcept
            : profiling_requested_(other.profiling_requested_)
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

            Internal::Execution::RuntimeContext& runtime =
                Internal::ContextAccess::context(context);
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
            runtime.execution_engine().prepare(built->plan, built->placement->rules);
            built->plan.profiling = profiling_requested_;
            built->plan.capture_base_state();
            built->plan.chain = Internal::Execution::classify_chain(built->plan, runtime);
            state_ = std::move(built);
            registrations_ = std::move(registrations);
            dirty_ = false;
            built_generation_ = recorder.generation();
            ++compiles_;
            return {};
        }

        bool PlanCache::stale(const Recorder& recorder, const ContextHandle& context) const noexcept
        {
            return state_ == nullptr || dirty_ || built_generation_ != recorder.generation() ||
                   state_->placement->generation !=
                       Internal::ContextAccess::context(context).placement()->generation;
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
                described.operation = node.metadata.op_id;
                described.placement_class = node.metadata.placement_class;
                described.placement_key = node.metadata.placement_key;
                described.determinism = node.metadata.determinism;
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

        void PlanCache::take(PlanCache& other) noexcept
        {
            state_ = std::move(other.state_);
            registrations_ = std::exchange(other.registrations_, {});
            built_generation_ = std::exchange(other.built_generation_, 0);
            compiles_ = std::exchange(other.compiles_, 0);
            dirty_ = std::exchange(other.dirty_, true);
            profiling_requested_ = other.profiling_requested_;
        }
    } // namespace Detail
} // namespace SushiRuntime
