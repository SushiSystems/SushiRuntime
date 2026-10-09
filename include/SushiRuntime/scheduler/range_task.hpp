/**************************************************************************/
/* range_task.hpp                                                         */
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
 * @file range_task.hpp
 * @brief Declares RangeTask, a splittable task over a contiguous index range.
 * @author Mustafa Garip
 * @see include/SushiRuntime/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Represents a splittable work-stealing task over a contiguous index range.
         * @see include/SushiRuntime/scheduler/README.md
         */
        class SUSHIRUNTIME_API RangeTask : public Task
        {
            private:
                /** @brief Atomic start index of the current range. */
                std::atomic<size_t> start_;
                /** @brief Fixed end index of the range. */
                size_t end_;
                /** @brief The function to execute for each sub-range. */
                std::function<void(size_t, size_t, sycl::handler&)> kernel_func_;
                /** @brief Label this range and every range split from it reports under. */
                const char* name_;

            public:
                /**
                 * @brief Creates a new range task.
                 * @param name Persistent label, stored not copied.
                 */
                template <typename Func>
                RangeTask(size_t start, size_t end, Func func, const char* name = "range_task")
                    : start_(start), end_(end), kernel_func_(std::move(func)), name_(name) {}

                /** @brief Returns the label given at construction. */
                const char* get_name() const noexcept override { return name_; }

                /** @brief Executes the task for the current valid range. */
                void execute(sycl::handler& h) override 
                {
                    size_t s = start_.load(std::memory_order_acquire);
                    if (s < end_) kernel_func_(s, end_, h);
                }

                /**
                 * @brief Splits the range task at the midpoint.
                 * 
                 * Uses atomic compare-exchange to hand over part of the work.
                 * 
                 * @return optional sushi_ptr to the new range task.
                 */
                std::optional<sushi_ptr<Task>> split() override 
                {
                    size_t s = start_.load(std::memory_order_acquire);
                    size_t mid = s + (end_ - s) / 2;

                    // prevent splitting if the range is too small
                    if (mid <= s || (end_ - mid) < 1024) return std::nullopt;

                    if (start_.compare_exchange_strong(s, mid, std::memory_order_acq_rel)) 
                        return make_sushi<RangeTask>(s, mid, kernel_func_, name_);
                    
                    return std::nullopt;
                }

                /** @brief Returns true if the range is large enough to split. */
                bool is_splittable() const override 
                {
                    return (end_ - start_.load()) > 1024;
                }
        };

    } // namespace Scheduler
} // namespace SushiRuntime
