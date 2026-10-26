/**************************************************************************/
/* run_observer.hpp                                                       */
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
 * @file run_observer.hpp
 * @brief Declares RunObserver, RunObservation and RunObserverHandle, the way a run's timing leaves the runtime.
 * @author Mustafa Garip
 * @see modules/foundation/report/README.md
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <SushiRuntime/report/clock_pairing.hpp>
#include <SushiRuntime/report/drop_counts.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/report/node_table.hpp>
#include <SushiRuntime/report/run_report.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds what the runtime captured of one finished run.
     *
     * Valid for the duration of RunObserver::on_run(); an observer that keeps a part copies it.
     * @see modules/foundation/report/README.md
     */
    struct RunObservation
    {
        ExecutionLayer layer = ExecutionLayer::Scheduler; /**< Path that submitted the run's device work. */
        std::shared_ptr<const NodeTable> table;           /**< Names, operations and costs of the plan's nodes, shared by every run of the plan. */
        std::vector<NodeEventRecord> events;              /**< One record per dispatch; a device record is on its device's event clock, a host record on the steady clock. */
        std::vector<DeviceClock> device_clocks;           /**< One pairing per device that ran a kernel of the run. */
        DropCounts drops;                                 /**< Dispatches the run lost, per cause. */
        std::uint64_t graph_id = 0;                       /**< Id of the graph that owns the run's plan; 0 when none is known. */
    };

    /**
     * @brief Receives the capture of every finished run of a runtime it was added to.
     *
     * A runtime records device timing while it has at least one observer.
     * @see modules/foundation/report/README.md
     */
    class RunObserver
    {
        public:
            virtual ~RunObserver() = default;

            /**
             * @brief Receives the capture of one finished run, on the thread that learned the run finished.
             *
             * Called once per run while the observer is added, after the run finished without a
             * fault, with no records for a run of a plan built before the observer was added. Must not
             * throw, must not start a run and must not add or remove an observer.
             */
            virtual void on_run(const RunObservation& observation) noexcept = 0;
    };

    /**
     * @brief Removes the observer it stands for from its runtime when it is destroyed or reset.
     *
     * Move-only. A handle that outlives its runtime removes nothing.
     * @see modules/foundation/report/README.md
     */
    class RunObserverHandle final
    {
        public:
            /** @brief Builds a handle that stands for no observer. */
            RunObserverHandle() noexcept = default;

            /** @brief Builds a handle that calls @p remove once, to take its observer out. */
            explicit RunObserverHandle(std::function<void()> remove) noexcept
                : remove_(std::move(remove))
            {
            }

            RunObserverHandle(RunObserverHandle&& other) noexcept
                : remove_(std::exchange(other.remove_, nullptr))
            {
            }

            RunObserverHandle& operator=(RunObserverHandle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    remove_ = std::exchange(other.remove_, nullptr);
                }
                return *this;
            }

            RunObserverHandle(const RunObserverHandle&) = delete;
            RunObserverHandle& operator=(const RunObserverHandle&) = delete;

            /** @brief Removes the observer, if the handle still stands for one. */
            ~RunObserverHandle()
            {
                reset();
            }

            /** @brief Removes the observer now; a second call does nothing. */
            void reset() noexcept
            {
                if (!remove_)
                    return;
                const std::function<void()> remove = std::exchange(remove_, nullptr);
                remove();
            }

            /** @brief Returns whether the handle stands for an observer. */
            [[nodiscard]] explicit operator bool() const noexcept
            {
                return static_cast<bool>(remove_);
            }

        private:
            /** @brief Takes the observer out of its runtime; empty once used. */
            std::function<void()> remove_;
    };
} // namespace SushiRuntime
