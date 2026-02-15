/**************************************************************************/
/* splittable_task.hpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once
#include <sycl/sycl.hpp>
#include <functional>
#include <atomic>
#include <optional>
#include "../core/sushi_ptr.hpp"

namespace SushiRuntime 
{
    namespace Scheduler
    {
        /**
         * @brief interface for tasks that can be split for load balancing.
         */
        class SplittableTask : public Core::RefCounted 
        {
        public:
            virtual ~SplittableTask() = default;

            /**
             * @brief main execution logic for the task.
             * @param h sycl handler to submit work.
             */
            virtual void execute(sycl::handler& h) = 0;

            /**
             * @brief splits the task into two. returns the new task part.
             * @return optional sushi_ptr to the new task part.
             */
            virtual std::optional<sushi_ptr<SplittableTask>> split() = 0;

            /**
             * @brief determines if the task is still large enough to be split.
             * @return true if the task can be split.
             */
            virtual bool is_splittable() const = 0;
        };

        /**
         * @brief example of a range-based task that can be split.
         */
        class RangeTask : public SplittableTask 
        {
            private:
                /** @brief atomic start index of the current range. */
                std::atomic<size_t> start_;
                /** @brief fixed end index of the range. */
                size_t end_;
                /** @brief the function to execute for each sub-range. */
                std::function<void(size_t, size_t, sycl::handler&)> kernel_func_;

            public:
                /** @brief creates a new range task. */
                RangeTask(size_t start, size_t end, auto func) 
                    : start_(start), end_(end), kernel_func_(func) {}

                /** @brief executes the task for the current valid range. */
                void execute(sycl::handler& h) override 
                {
                    size_t s = start_.load(std::memory_order_acquire);
                    if (s < end_) kernel_func_(s, end_, h);
                }

                /**
                 * @brief splits the range task at the midpoint.
                 * 
                 * uses atomic compare-exchange to hand over part of the work.
                 * 
                 * @return optional sushi_ptr to the new range task.
                 */
                std::optional<sushi_ptr<SplittableTask>> split() override 
                {
                    size_t s = start_.load(std::memory_order_acquire);
                    size_t mid = s + (end_ - s) / 2;

                    // prevent splitting if the range is too small
                    if (mid <= s || (end_ - mid) < 1024) return std::nullopt;

                    if (start_.compare_exchange_strong(s, mid, std::memory_order_acq_rel)) 
                    {
                        return make_sushi<RangeTask>(mid, end_, kernel_func_);
                    }
                    return std::nullopt;
                }

                /** @brief returns true if the range is large enough to split. */
                bool is_splittable() const override 
                {
                    return (end_ - start_.load()) > 1024;
                }
        };

    } // namespace Scheduler
} // namespace SushiRuntime