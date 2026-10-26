/**************************************************************************/
/* run_feed.hpp                                                           */
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
 * @file run_feed.hpp
 * @brief Declares RunFeed, the observer that puts a runtime's runs on the source's queue.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include <SushiRuntime/profiling/run_queue.hpp>
#include <SushiRuntime/report/run_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        /** @brief Copies every observation of one runtime into a RunQueue, tagged with the runtime. */
        class RunFeed final : public RunObserver
        {
            public:
                /** @brief Builds the feed of runtime @p runtime into @p queue, which the feed keeps alive. */
                RunFeed(std::shared_ptr<RunQueue> queue, std::uint32_t runtime) noexcept
                    : queue_(std::move(queue)), runtime_(runtime)
                {
                }

                /** @brief Queues @p observation; a failure to allocate drops it. */
                void on_run(const RunObservation& observation) noexcept override
                {
                    try
                    {
                        queue_->push(runtime_, observation);
                    }
                    catch (...)
                    {
                        // The observer contract forbids throwing out of on_run, so the run's timing is lost.
                    }
                }

            private:
                /** @brief The queue the source drains. */
                std::shared_ptr<RunQueue> queue_;

                /** @brief Index of the runtime in the source's registration order. */
                std::uint32_t runtime_;
        };
    } // namespace Profiling
} // namespace SushiRuntime
