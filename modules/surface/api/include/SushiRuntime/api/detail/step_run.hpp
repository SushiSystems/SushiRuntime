/**************************************************************************/
/* step_run.hpp                                                           */
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
 * @file step_run.hpp
 * @brief Declares the calls a step driver makes into the library to run a built plan.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/api/detail/before_step_hooks.hpp>
#include <SushiRuntime/api/detail/plan.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/api/runtime/context_handle.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/report/run_report.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Refers to the stop test of a run without owning it or naming its type.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        class StepPredicate final
        {
            public:
                /**
                 * @brief Refers to a callable invoked as keep_going(completed_steps).
                 * @param keep_going Outlives this object; true continues the run.
                 */
                template <class KeepGoing>
                explicit StepPredicate(KeepGoing& keep_going) noexcept
                    : object_(&keep_going), call_(&StepPredicate::invoke<KeepGoing>)
                {
                }

                /** @brief Returns whether the run takes another step. */
                [[nodiscard]] bool operator()(std::size_t completed) const
                {
                    return call_(object_, completed);
                }

            private:
                /** @brief Calls the test behind object as the type it was given with. */
                template <class KeepGoing>
                static bool invoke(void* object, std::size_t completed)
                {
                    return (*static_cast<KeepGoing*>(object))(completed);
                }

                /** @brief The caller's stop test. */
                void* object_;

                /** @brief Restores the type of object_ and calls it. */
                bool (*call_)(void*, std::size_t);
        };

        /**
         * @brief Names what one graph hands the library to run its plan.
         *
         * Every pointer is borrowed for the length of one call.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct StepTarget
        {
            const ContextHandle* context = nullptr;         /**< Runtime the plan runs on. */
            Plan* plan = nullptr;                           /**< The built plan. */
            const StepMachinery* steps = nullptr;           /**< Per-step hooks of the nodes. */
            const BeforeStepHooks* hooks = nullptr;         /**< The caller's per-step hooks. */
            void* owner = nullptr;                          /**< The graph, handed to rearm. */
            void (*rearm)(void* owner) = nullptr;           /**< Restores the plan's run state. */
            std::size_t node_count = 0;                     /**< Nodes one step dispatches. */
            bool may_chain = false;                         /**< Whether the graph allows a chain. */
        };

        /** @brief Returns whether a runtime asks every new graph for per-node device timing. */
        [[nodiscard]] SUSHIRUNTIME_API bool profiling_default(const ContextHandle& context) noexcept;

        /** @brief Says whether a chained run may skip the host wait between steps. */
        enum class StepSync : std::uint8_t
        {
            AtEnd,     /**< Fixed step count: submit back to back, one wait at the end. */
            EveryStep  /**< The stop test may read results: wait after every step. */
        };

        /**
         * @brief Runs steps over a built plan until the stop test ends the run, and fills the
         *        report.
         * @return Nothing, or the fault a step recorded; the report is not filled then.
         * @throws Whatever a step throws.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> run_steps(const StepTarget& target,
                                                              StepPredicate keep_going,
                                                              RunReport& report,
                                                              double compile_ms,
                                                              std::size_t expected_invocations,
                                                              StepSync sync);

        /**
         * @brief Submits one step to the worker pool without waiting for it.
         * @param state Receives what finish_step() needs; compile_ms and serial are kept.
         * @return Nothing, or why the engine did not take the step.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> submit_step(const StepTarget& target,
                                                                AsyncRunState& state);

        /**
         * @brief Closes out a submitted step once complete_step() returned success.
         * @param report The report to fill; reset before it is written.
         * @return Nothing, or the fault the step recorded; no State advances then.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> finish_step(const StepTarget& target,
                                                                const AsyncRunState& state,
                                                                RunReport& report);

        /** @brief Waits for the step submitted on a runtime and returns how it ended. */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> complete_step(const ContextHandle& context);

        /** @brief Returns whether waiting for the submitted step would return at once. */
        [[nodiscard]] SUSHIRUNTIME_API bool step_finished(const ContextHandle& context);

        /** @brief Returns whether a runtime's engine starts a run without blocking. */
        [[nodiscard]] SUSHIRUNTIME_API bool supports_asynchronous_run(const ContextHandle& context);

        /** @brief Returns whether a run is in flight on a runtime. */
        [[nodiscard]] SUSHIRUNTIME_API bool run_in_progress(const ContextHandle& context) noexcept;

        /** @brief Asks the run in flight on a runtime to stop. */
        SUSHIRUNTIME_API void cancel_run(const ContextHandle& context) noexcept;
    } // namespace Detail
} // namespace SushiRuntime
