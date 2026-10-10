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

                PlanCache(PlanCache&&) = default;
                PlanCache& operator=(PlanCache&&) = default;
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
                    registrations_.assign(recorder.node_count(), BackingGraph::NO_REGISTRATION);
                    recorder.replay(bg, [this, &bg](std::size_t node)
                    {
                        registrations_[node] = bg.last_registration_index();
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
                 * @brief Returns the registration index a node took at the last build.
                 * @param node Position in recording order.
                 * @return The index, or BackingGraph::NO_REGISTRATION for a node no build has seen.
                 */
                std::uint32_t registration_of(std::size_t node) const noexcept
                {
                    return node < registrations_.size() ? registrations_[node]
                                                        : BackingGraph::NO_REGISTRATION;
                }

                /** @brief The compiled plan, valid while stale() is false; test seams read it. */
                ::SushiRuntime::Graph::CompiledGraph plan_;

            private:
                /** @brief Registration index of each node, as the last build stamped it. */
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
