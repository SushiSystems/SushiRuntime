/**************************************************************************/
/* barrier.hpp                                                            */
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
 * @file barrier.hpp
 * @brief Declares a reusable thread barrier for tests compiled as C++17.
 * @author Mustafa Garip
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Holds arriving threads until a set number have arrived, then releases them all.
         */
        class Barrier
        {
            public:
                /**
                 * @brief Builds a barrier that releases every count-th arrival.
                 * @param count Number of threads that must arrive before release; at least 1.
                 */
                explicit Barrier(std::size_t count)
                    : threshold_(count), remaining_(count), generation_(0)
                {
                }

                /**
                 * @brief Blocks until `count` threads have arrived at this barrier.
                 *
                 * The last arrival resets the counter, advances the generation, and
                 * wakes the rest. Earlier arrivals wait on the generation they came
                 * in on, so the barrier is safe to reuse phase after phase.
                 */
                void arrive_and_wait()
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    const std::size_t gen = generation_;
                    if (--remaining_ == 0)
                    {
                        remaining_ = threshold_;
                        ++generation_;
                        cv_.notify_all();
                    }
                    else
                    {
                        cv_.wait(lock, [this, gen] { return gen != generation_; });
                    }
                }

            private:
                std::mutex mutex_;
                std::condition_variable cv_;
                std::size_t threshold_;
                std::size_t remaining_;
                std::size_t generation_;
        };
    } // namespace Test
} // namespace SushiRuntime
