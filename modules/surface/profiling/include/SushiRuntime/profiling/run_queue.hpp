/**************************************************************************/
/* run_queue.hpp                                                          */
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
 * @file run_queue.hpp
 * @brief Declares RunQueue, the bounded hand-over of observations from the runs' threads to the consumer.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/report/run_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        /** @brief Holds one observation and the runtime that made it. */
        struct QueuedRun
        {
            std::uint32_t runtime = 0;  /**< Index of the runtime in the source's registration order. */
            RunObservation observation; /**< What the run captured. */
        };

        /**
         * @brief Holds the observations the runs made until the consumer thread takes them.
         *
         * Any number of threads push and one takes. A full queue drops its oldest observation and
         * counts the records it held, so memory stays bounded when no session collects.
         * @see modules/surface/profiling/README.md
         */
        class SUSHIRUNTIME_API RunQueue final
        {
            public:
                /** @brief Builds a queue that holds at most @p capacity observations; 0 holds one. */
                explicit RunQueue(std::size_t capacity) noexcept;

                RunQueue(const RunQueue&) = delete;
                RunQueue& operator=(const RunQueue&) = delete;
                RunQueue(RunQueue&&) = delete;
                RunQueue& operator=(RunQueue&&) = delete;
                ~RunQueue() = default;

                /** @brief Copies @p observation in, dropping the oldest one when the queue is full. */
                void push(std::uint32_t runtime, const RunObservation& observation);

                /** @brief Takes every observation, oldest first. */
                [[nodiscard]] std::vector<QueuedRun> take();

                /** @brief Drops what was never taken; returns how many observations that was. */
                std::size_t discard() noexcept;

                /** @brief Returns how many records full-queue drops took since the last call, and starts again. */
                [[nodiscard]] std::uint64_t take_overflow() noexcept;

                /** @brief Returns how many observations are held. */
                [[nodiscard]] std::size_t size() const;

            private:
                /** @brief The most observations held. */
                std::size_t capacity_;

                /** @brief Guards the members below. */
                mutable std::mutex mutex_;

                /** @brief The held observations, oldest first. */
                std::deque<QueuedRun> runs_;

                /** @brief Records of the observations a full queue dropped, since the last take_overflow(). */
                std::uint64_t overflow_ = 0;
        };
    } // namespace Profiling
} // namespace SushiRuntime
