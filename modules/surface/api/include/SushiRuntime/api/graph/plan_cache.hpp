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

#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

namespace SushiRuntime
{
    namespace API
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
                 * @brief Number of times the graph has been compiled.
                 *
                 * Exposes the replay guarantee: a graph that only changes late-bound
                 * size or enablement between steps recompiles zero extra times, so
                 * this stays at 1 across a whole run after the first build.
                 * @return The compile count since this Graph was created.
                 */
                std::size_t compile_count() const noexcept { return compiles_; }

                /**
                 * @brief Requests or withdraws per-node device timing and marks the plan dirty.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void set_profiling_requested(bool requested) noexcept
                {
                    profiling_requested_ = requested;
                    dirty_ = true;
                }

                /**
                 * @brief Reports whether this graph currently requests profiling.
                 * @return The last value set by set_profiling_requested(), or the
                 *         runtime's RuntimeSettings::profiling default.
                 */
                bool profiling_requested() const noexcept { return profiling_requested_; }

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

                /** @brief Returns whether the plan is missing or older than what recorder holds. */
                bool stale(const Recorder& recorder) const noexcept
                {
                    return dirty_ || built_generation_ != recorder.generation();
                }

                /**
                 * @brief Replays the recorder onto a fresh backing graph and keeps the plan.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void build(const Recorder& recorder, Execution::RuntimeContext& context)
                {
                    BackingGraph bg(context.offload());
                    registrations_.assign(recorder.rule_count(), BackingGraph::NO_REGISTRATION);
                    recorder.replay(bg, [this, &bg](std::size_t rule, std::size_t)
                    {
                        registrations_[rule] = bg.last_registration_index();
                    });
                    plan_ = bg.compile();
                    plan_.profiling = profiling_requested_;
                    plan_.capture_base_state();
                    plan_.chain = Execution::classify_chain(plan_, context);
                    dirty_ = false;
                    built_generation_ = recorder.generation();
                    ++compiles_;
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
                 * @return The index, or BackingGraph::NO_REGISTRATION for a rule no build has seen.
                 */
                [[nodiscard]] std::uint32_t registration_of(std::size_t rule) const noexcept
                {
                    return rule < registrations_.size() ? registrations_[rule]
                                                        : BackingGraph::NO_REGISTRATION;
                }

                /** @brief The compiled plan, valid while stale() is false; test seams read it. */
                ::SushiRuntime::Graph::CompiledGraph plan_;

            private:
                /**
                 * @brief Moves other's plan and counters here and leaves other with none.
                 * @param other Cache to empty; its profiling request is copied, not cleared.
                 */
                void take(PlanCache& other) noexcept
                {
                    plan_ = std::exchange(other.plan_, {});
                    registrations_ = std::exchange(other.registrations_, {});
                    built_generation_ = std::exchange(other.built_generation_, 0);
                    compiles_ = std::exchange(other.compiles_, 0);
                    dirty_ = std::exchange(other.dirty_, true);
                    profiling_requested_ = other.profiling_requested_;
                }

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

    } // namespace API
} // namespace SushiRuntime
