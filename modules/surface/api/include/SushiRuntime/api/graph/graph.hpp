/**************************************************************************/
/* graph.hpp                                                              */
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
 * @file graph.hpp
 * @brief Declares Graph, the fluent builder that records tasks and runs them.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <SushiRuntime/api/detail/error_translation.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/graph/builder_surface.hpp>
#include <SushiRuntime/api/graph/plan_cache.hpp>
#include <SushiRuntime/api/graph/plan_view.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/api/graph/step_driver.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/observability/run_report.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Grants the registration index tests a friend seam onto Graph::plan_. */
        struct RegistrationIndexTestAccess;

        /** @brief Grants the node event record tests a friend seam onto Graph::plan_. */
        struct NodeEventRecordsTestAccess;
    }

    /**
     * @brief A chainable graph of rules built fluently, then run.
     *
     * Every add() overload appends one rule and returns *this so calls chain. Obtain one
     * from Runtime::graph(). A graph moves and does not copy; the source of a move is left
     * empty on the same runtime and can be built and run again.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    class Graph : public BuilderSurface<Graph>,
                  public StepDriver<Graph>,
                  private PlanCache
    {
        public:
            /**
             * @brief Takes over another graph's rules and compiled plan.
             * @param other Left empty on the same runtime: size() and compile_count() are zero.
             * @pre No step started with run_async() on other is pending.
             */
            Graph(Graph&& other) noexcept
                : BuilderSurface<Graph>(std::move(other)),
                  StepDriver<Graph>(std::move(other)),
                  PlanCache(std::move(other)),
                  context_(other.context_),
                  recorder_(std::move(other.recorder_))
            {
            }

            /**
             * @brief Replaces this graph's rules and plan with another's.
             * @param other Left empty on the same runtime: size() and compile_count() are zero.
             * @pre No step started with run_async() on either graph is pending.
             */
            Graph& operator=(Graph&& other) noexcept
            {
                if (this != &other)
                {
                    StepDriver<Graph>::operator=(std::move(other));
                    PlanCache::operator=(std::move(other));
                    context_ = other.context_;
                    recorder_ = std::move(other.recorder_);
                }
                return *this;
            }

            Graph(const Graph&) = delete;
            Graph& operator=(const Graph&) = delete;

            /**
             * @brief Waits for a step started with run_async() and still pending, then frees
             *        the rules and the plan.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            ~Graph()
            {
                // The plan and the recorder are alive here and gone in a base destructor.
                end_pending_step();
            }

            /** @brief Returns the recorder this graph's rules are recorded on. */
            [[nodiscard]] Recorder& recorder() noexcept { return recorder_; }

            /** @brief Returns the recorder this graph's rules are recorded on, read-only. */
            [[nodiscard]] const Recorder& recorder() const noexcept { return recorder_; }

            /** @brief Stands for "this rule has no index yet". */
            static constexpr std::uint32_t K_NO_REGISTRATION =
                ::SushiRuntime::Internal::Graph::TaskGraph::K_NO_REGISTRATION;

            /**
             * @brief Returns the registration index the rule at position rule took.
             * @param rule Position in add() order, below size(); last_rule() gives the newest.
             * @return The index every node of the rule reports under, or K_NO_REGISTRATION when
             *        rule is out of range or nothing compiled yet.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] std::uint32_t rule_registration(std::size_t rule) const noexcept
            {
                return registration_of(rule);
            }

            /**
             * @brief Returns a copy of what the plan orders, building the plan when none is
             *        current.
             * @return One entry per recorded node in recording order, or the Error compile()
             *         returns.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] Result<PlanView> plan() noexcept
            {
                Result<void> compiled = compile();
                if (!compiled)
                    return make_error(std::move(compiled.error()));

                PlanView copied;
                Error failure = Detail::translate_exceptions(
                    ErrorCode::CompileFailed, ErrorCode::CompileFailed,
                    [this, &copied]() { copied = describe_plan(); });
                if (failure.code != ErrorCode::Ok)
                    return make_error(std::move(failure));
                return copied;
            }

            using PlanCache::compile_count;
            using StepDriver<Graph>::is_profiling_requested;
            using StepDriver<Graph>::set_profiling;

        private:
            friend class Runtime;
            // The driver reaches the plan, the hooks and the checks below, and nothing else.
            friend class StepDriver<Graph>;
            friend struct Detail::RegistrationIndexTestAccess;
            friend struct Detail::NodeEventRecordsTestAccess;

            /** @brief Error text of a run started with nothing recorded. */
            static constexpr const char* K_NOTHING_TO_RUN = "Graph has no tasks to run.";

            /** @brief Error text of a run whose recorded handle was freed. */
            static constexpr const char* K_DANGLING_RESOURCE =
                "Graph references a Buffer/State freed before the run.";

            explicit Graph(std::shared_ptr<Internal::Execution::RuntimeContext> context)
                : PlanCache(context->settings().profiling), context_(std::move(context)),
                recorder_(context_)
            {
            }

            /** @brief Returns the context whose engine runs this graph. */
            Internal::Execution::RuntimeContext& run_context() noexcept { return *context_; }

            /** @brief Records the profiling request; a changed one rebuilds the plan. */
            void request_profiling(bool requested) noexcept
            {
                PlanCache::set_profiling(requested);
            }

            /** @brief Returns whether the next plan requests per-node device timing. */
            [[nodiscard]] bool profiling_request() const noexcept
            {
                return PlanCache::is_profiling_requested();
            }

            /** @brief Returns whether any task has been recorded. */
            [[nodiscard]] bool has_work() const noexcept
            {
                return recorder_.node_count() != 0;
            }

            /** @brief Returns whether any recorded handle's allocation has been freed. */
            [[nodiscard]] bool has_dangling_resource() const noexcept
            {
                return recorder_.has_dangling_resource();
            }

            /** @brief Returns the first refused recording, or null when there is none. */
            [[nodiscard]] const Error* recording_refusal() const noexcept
            {
                return recorder_.refusal();
            }

            /** @brief Returns whether the plan must be built before the next step. */
            [[nodiscard]] bool plan_is_stale() const noexcept { return stale(recorder_); }

            /** @brief Builds the plan from the recorder, or returns why it has none. */
            [[nodiscard]] Result<void> rebuild_plan() { return build(recorder_, *context_); }

            /** @brief Returns success: a built plan needs no further preparation. */
            [[nodiscard]] Result<void> prepare_run() noexcept { return {}; }

            /** @brief Returns the plan a step runs. */
            ::SushiRuntime::Internal::Graph::CompiledGraph& run_plan() noexcept { return plan_; }

            /** @brief Returns the per-step hooks of the recorded tasks. */
            [[nodiscard]] const Detail::StepMachinery& run_steps() const noexcept
            {
                return recorder_.steps();
            }

            /** @brief Restores the plan's per-run state before a step. */
            void rearm_plan() noexcept { rearm(); }

            /** @brief Returns whether the steps run as an in-order chain from this thread. */
            [[nodiscard]] bool runs_chained() const
            {
                return plan_.chain.chainable && !context_->settings().force_scheduler;
            }

            std::shared_ptr<Internal::Execution::RuntimeContext> context_;

            /** @brief Owns the recorded tasks, their scratch and their per-step hooks. */
            Recorder recorder_;
    };
} // namespace SushiRuntime
