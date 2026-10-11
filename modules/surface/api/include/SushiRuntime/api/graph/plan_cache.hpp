/**************************************************************************/
/* plan_cache.hpp                                                         */
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
 * @file plan_cache.hpp
 * @brief Declares PlanCache, the compiled plan of one recorder.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <SushiRuntime/api/graph/plan_view.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

namespace SushiRuntime
{
    /**
     * @brief Keeps the plan compiled from one recorder and tells when it must be rebuilt.
     *
     * A move leaves the source with no plan and a compile count of zero.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    class PlanCache
    {
        public:
            /**
             * @brief Returns how many times the plan has been built.
             * @return The count since this cache was created or last moved from.
             */
            [[nodiscard]] std::size_t compile_count() const noexcept { return compiles_; }

        protected:
            /**
             * @brief Starts with no plan built.
             * @param profiling Whether the first plan requests per-node device timing.
             */
            explicit PlanCache(bool profiling) noexcept : profiling_requested_(profiling)
            {
            }

            /**
             * @brief Takes over another cache's plan.
             * @param other Left with no plan, stale, and a compile count of zero.
             */
            PlanCache(PlanCache&& other) noexcept
                : profiling_requested_(other.profiling_requested_)
            {
                take(other);
            }

            /**
             * @brief Replaces this cache's plan with another's.
             * @param other Left with no plan, stale, and a compile count of zero.
             */
            PlanCache& operator=(PlanCache&& other) noexcept
            {
                if (this != &other)
                    take(other);
                return *this;
            }

            PlanCache(const PlanCache&) = delete;
            PlanCache& operator=(const PlanCache&) = delete;
            ~PlanCache() = default;

            /**
             * @brief Requests or withdraws per-node device timing.
             *
             * A value that differs from the stored one marks the plan stale; the same value
             * changes nothing.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void set_profiling(bool requested) noexcept
            {
                if (requested == profiling_requested_)
                    return;
                profiling_requested_ = requested;
                dirty_ = true;
            }

            /**
             * @brief Returns whether the next plan requests per-node device timing.
             * @return The last value given to set_profiling(), or the value the cache was
             *         constructed with.
             */
            [[nodiscard]] bool is_profiling_requested() const noexcept
            {
                return profiling_requested_;
            }

            /** @brief Returns whether the plan is missing or older than what recorder holds. */
            [[nodiscard]] bool stale(const Recorder& recorder) const noexcept
            {
                return dirty_ || built_generation_ != recorder.generation();
            }

            /**
             * @brief Replays the recorder onto a fresh backing graph and keeps the plan.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] Result<void> build(const Recorder& recorder,
                                             Internal::Execution::RuntimeContext& context)
            {
                ::SushiRuntime::Internal::Graph::TaskGraph backing_graph(context.offload());
                registrations_.assign(recorder.rule_count(),
                                      ::SushiRuntime::Internal::Graph::TaskGraph::K_NO_REGISTRATION);
                recorded_.clear();
                recorded_.reserve(recorder.node_count());
                recorder.replay(backing_graph, [this, &backing_graph](std::size_t rule, std::size_t)
                {
                    const ::SushiRuntime::Internal::Graph::Node* added =
                        backing_graph.last_node_added();
                    if (added)
                    {
                        registrations_[rule] = added->registration_index;
                        recorded_.push_back(RecordedNode{added, rule});
                    }
                });
                Result<::SushiRuntime::Internal::Graph::CompiledGraph> compiled =
                    backing_graph.compile();
                if (!compiled)
                {
                    // The nodes recorded_ names died with the backing graph.
                    recorded_.clear();
                    return make_error(std::move(compiled.error()));
                }
                plan_ = std::move(*compiled);
                plan_.profiling = profiling_requested_;
                plan_.capture_base_state();
                plan_.chain = Internal::Execution::classify_chain(plan_, context);
                dirty_ = false;
                built_generation_ = recorder.generation();
                ++compiles_;
                return {};
            }

            /**
             * @brief Restores the cached plan's per-run state so it can run again.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void rearm() noexcept
            {
                plan_.rearm();
            }

            /**
             * @brief Returns the registration index a rule took at the last build.
             * @param rule Position in recording order, one per rule.
             * @return The index, or TaskGraph::K_NO_REGISTRATION for a rule no build has seen.
             */
            [[nodiscard]] std::uint32_t registration_of(std::size_t rule) const noexcept
            {
                return rule < registrations_.size()
                    ? registrations_[rule] : ::SushiRuntime::Internal::Graph::TaskGraph::K_NO_REGISTRATION;
            }

            /**
             * @brief Copies what the cached plan orders, one entry per recorded node.
             * @pre stale() is false for the recorder the plan was built from.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] PlanView describe_plan() const
            {
                PlanView view;
                view.nodes.reserve(recorded_.size());
                for (const RecordedNode& recorded : recorded_)
                {
                    const ::SushiRuntime::Internal::Graph::Node& node = *recorded.node;
                    PlanNode described;
                    described.rule = recorded.rule;
                    if (node.metadata.name != nullptr)
                        described.name = node.metadata.name;
                    described.operation = node.metadata.op_id;
                    described.predecessor_count =
                        static_cast<std::size_t>(plan_.base_in_degree[node.plan_index]);
                    described.reads = node.io_reads_;
                    described.writes = node.io_writes_;
                    view.nodes.push_back(std::move(described));
                }
                return view;
            }

            /** @brief The compiled plan, valid while stale() is false; test seams read it. */
            ::SushiRuntime::Internal::Graph::CompiledGraph plan_;

        private:
            /**
             * @brief Moves other's plan and counters here and leaves other with none.
             * @param other Cache to empty; its profiling request is copied, not cleared.
             */
            void take(PlanCache& other) noexcept
            {
                plan_ = std::exchange(other.plan_, {});
                recorded_ = std::exchange(other.recorded_, {});
                registrations_ = std::exchange(other.registrations_, {});
                built_generation_ = std::exchange(other.built_generation_, 0);
                compiles_ = std::exchange(other.compiles_, 0);
                dirty_ = std::exchange(other.dirty_, true);
                profiling_requested_ = other.profiling_requested_;
            }

            /** @brief Names one node of plan_ and the rule that recorded it. */
            struct RecordedNode
            {
                const ::SushiRuntime::Internal::Graph::Node* node = nullptr; /**< Owned by plan_. */
                std::size_t rule = 0; /**< Position of the rule in recording order. */
            };

            /** @brief The nodes of plan_ in recording order; plan_ lists them in its own. */
            std::vector<RecordedNode> recorded_;

            /** @brief Registration index of each rule, as the last build stamped it. */
            std::vector<std::uint32_t> registrations_;

            /** @brief Recorder generation the plan was built at. */
            std::uint64_t built_generation_ = 0;

            /** @brief Counts how many times the backing graph has been compiled. */
            std::size_t compiles_ = 0;

            /** @brief Set until the first build, and by a change of the profiling request. */
            bool dirty_ = true;

            /** @brief Starts as RuntimeSettings::profiling; stamped into the plan at build. */
            bool profiling_requested_ = false;
    };
} // namespace SushiRuntime
