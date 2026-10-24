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
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/detail/plan.hpp>
#include <SushiRuntime/api/graph/plan_view.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    struct NodeFacts;

    namespace Detail
    {
        struct PlanCacheState;

        /**
         * @brief Keeps the plan compiled from one recorder and tells when it must be rebuilt.
         *
         * A move leaves the source with no plan and a compile count of zero.
         * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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
                SUSHIRUNTIME_API explicit PlanCache(bool profiling) noexcept;

                /**
                 * @brief Takes over another cache's plan.
                 * @param other Left with no plan, stale, and a compile count of zero.
                 */
                SUSHIRUNTIME_API PlanCache(PlanCache&& other) noexcept;

                /**
                 * @brief Replaces this cache's plan with another's.
                 * @param other Left with no plan, stale, and a compile count of zero.
                 */
                SUSHIRUNTIME_API PlanCache& operator=(PlanCache&& other) noexcept;

                PlanCache(const PlanCache&) = delete;
                PlanCache& operator=(const PlanCache&) = delete;

                /** @brief Frees the plan. */
                SUSHIRUNTIME_API ~PlanCache();

                /**
                 * @brief Requests or withdraws per-node device timing.
                 *
                 * A value that differs from the stored one marks the plan stale; the same value
                 * changes nothing.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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

                /** @brief Marks the plan stale and forgets the registration of every rule. */
                void discard() noexcept
                {
                    registrations_.clear();
                    dirty_ = true;
                }

                /**
                 * @brief Returns whether the plan is missing or older than what recorder holds
                 *        or than the placement rules in force.
                 * @param context The runtime whose rule set the plan was compiled under.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                [[nodiscard]] SUSHIRUNTIME_API bool
                stale(const Recorder& recorder, const ContextHandle& context) const noexcept;

                /**
                 * @brief Replays the recorder onto a fresh backing graph and keeps the plan.
                 * @param here This machine's facts, which the rules' targets are read against;
                 *             null names no local device.
                 * @return Nothing, the compile's error, or the refusal of a Fail rule whose
                 *         target is absent while no offload coordinator is installed.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                [[nodiscard]] SUSHIRUNTIME_API Result<void>
                build(const Recorder& recorder, const ContextHandle& context,
                      const NodeFacts* here);

                /**
                 * @brief Restores the cached plan's per-run state so it can run again.
                 * @pre A plan is built.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                SUSHIRUNTIME_API void rearm() noexcept;

                /**
                 * @brief Returns the registration index a rule took at the last build.
                 * @param rule Position in recording order, one per rule.
                 * @return The index, or Detail::K_NO_REGISTRATION for a rule no build has seen.
                 */
                [[nodiscard]] std::uint32_t registration_of(std::size_t rule) const noexcept
                {
                    return rule < registrations_.size()
                        ? registrations_[rule] : Detail::K_NO_REGISTRATION;
                }

                /**
                 * @brief Copies what the cached plan orders, one entry per recorded node.
                 * @pre stale() is false for the recorder and context the plan was built from.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                [[nodiscard]] SUSHIRUNTIME_API PlanView describe_plan() const;

                /**
                 * @brief Returns the compiled plan; the owning graph and its test seams read it.
                 * @pre A plan is built.
                 */
                [[nodiscard]] SUSHIRUNTIME_API Plan* compiled_plan() noexcept;

                /**
                 * @brief Returns the compiled plan, read-only.
                 * @pre A plan is built.
                 */
                [[nodiscard]] SUSHIRUNTIME_API const Plan* compiled_plan() const noexcept;

            private:
                /**
                 * @brief Moves other's plan and counters here and leaves other with none.
                 * @param other Cache to empty; its profiling request is copied, not cleared.
                 */
                void take(PlanCache& other) noexcept;

                /** @brief The plan and the nodes it names; null until the first build. */
                std::unique_ptr<Detail::PlanCacheState> state_;

                /** @brief Registration index of each rule, as the last build stamped it. */
                std::vector<std::uint32_t> registrations_;

                /** @brief Recorder generation the plan was built at. */
                std::uint64_t built_generation_ = 0;

                /** @brief Counts how many times the backing graph has been compiled. */
                std::size_t compiles_ = 0;

                /** @brief Set until the first build, and by a change of the profiling request. */
                bool dirty_ = true;

                /** @brief Starts as the runtime's profiling setting; a build stamps it in. */
                bool profiling_requested_ = false;
        };
    } // namespace Detail
} // namespace SushiRuntime
