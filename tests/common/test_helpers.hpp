/**************************************************************************/
/* test_helpers.hpp                                                       */
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
 * @file test_helpers.hpp
 * @brief Declares the device probe, the host tasks and the USM guard that the test suites share.
 * @author Mustafa Garip
 * @see tests/common/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Returns whether SYCL reports at least one device of any type.
         *
         * Constructs no SushiRuntime object. An exception from SYCL device discovery
         * propagates to the caller, so a broken installation fails the test.
         */
        [[nodiscard]] inline bool has_any_sycl_device()
        {
            return !sycl::device::get_devices().empty();
        }

        /**
         * @brief Creates a host task that records its ID into a shared list.
         * @param execution_order List the task appends to; must outlive the task.
         * @param order_mutex Mutex that guards @p execution_order; must outlive the task.
         * @param delay Wait before recording, which widens the window for ordering bugs.
         */
        inline auto make_order_recorder(int task_id,
                                        std::vector<int>& execution_order,
                                        std::mutex& order_mutex,
                                        std::chrono::milliseconds delay = std::chrono::milliseconds(0))
        {
            return [task_id, &execution_order, &order_mutex, delay]
                   (sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
            {
                if (delay.count() > 0)
                    std::this_thread::sleep_for(delay);

                std::lock_guard<std::mutex> lock(order_mutex);
                execution_order.push_back(task_id);
                return {};
            };
        }

        /**
         * @brief Canonical scheduler workload that bumps an atomic counter.
         *
         * Performs a simple increment on the host, requiring no GPU involvement.
         */
        class AtomicCounterTask : public Internal::Scheduler::Task
        {
        public:
            /**
             * @brief Constructs an AtomicCounterTask.
             * @param c Reference to the atomic counter to increment.
             */
            explicit AtomicCounterTask(std::atomic<size_t>& c)
                : counter_(c) {}

            const char* get_name() const noexcept override { return "AtomicCounterTask"; }

            ExecutionMode get_execution_mode() const override { return ExecutionMode::HOST; }

            sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override
            {
                counter_.fetch_add(1, std::memory_order_relaxed);
                return {};
            }

            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
            bool is_splittable() const override             { return false; }

        private:
            std::atomic<size_t>& counter_;
        };

        /**
         * @brief Task that records its priority into a shared list.
         *
         * Used in scheduler tests to assert that priority ordering is respected.
         */
        class PriorityTrackingTask : public Internal::Scheduler::Task
        {
        public:
            /**
             * @brief Constructs a PriorityTrackingTask.
             * @param p The priority level of this task.
             * @param order Reference to the shared list for recording execution order.
             * @param mtx Mutex protecting the shared execution order list.
             */
            PriorityTrackingTask(int p, std::vector<int>& order, std::mutex& mtx)
                : priority_(p), execution_order_(order), order_mutex_(mtx) {}

            const char* get_name() const noexcept override    { return "PriorityTask"; }

            int get_priority() const noexcept override        { return priority_; }
            ExecutionMode get_execution_mode() const override { return ExecutionMode::HOST; }

            sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard<std::mutex> lock(order_mutex_);
                execution_order_.push_back(priority_);
                return {};
            }

            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
            bool is_splittable() const override             { return false; }

        private:
            int priority_;
            std::vector<int>& execution_order_;
            std::mutex& order_mutex_;
        };

        /**
         * @brief Move-only RAII guard for a sycl::malloc_shared buffer.
         *
         * Ensures that the allocated USM memory is automatically freed upon exit.
         */
        template <typename T>
        class USMShared
        {
        public:
            /**
             * @brief Allocates shared USM memory.
             * @param q The SYCL queue used for allocation.
             * @param count Number of elements of type T to allocate.
             */
            USMShared(sycl::queue& q, size_t count)
                : queue_(&q), ptr_(sycl::malloc_shared<T>(count, q)), count_(count) {}

            ~USMShared() { reset(); }

            USMShared(const USMShared&) = delete;
            USMShared& operator=(const USMShared&) = delete;

            USMShared(USMShared&& other) noexcept
                : queue_(other.queue_), ptr_(other.ptr_), count_(other.count_)
            {
                other.ptr_ = nullptr;
            }

            USMShared& operator=(USMShared&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    queue_ = other.queue_;
                    ptr_   = other.ptr_;
                    count_ = other.count_;
                    other.ptr_ = nullptr;
                }
                return *this;
            }

            T* get() const noexcept                  { return ptr_; }
            T& operator[](size_t i) const            { return ptr_[i]; }
            size_t count() const noexcept            { return count_; }
            explicit operator bool() const noexcept  { return ptr_ != nullptr; }

        private:
            void reset()
            {
                if (ptr_ && queue_)
                    sycl::free(ptr_, *queue_);
                ptr_ = nullptr;
            }

            sycl::queue* queue_ = nullptr;
            T* ptr_ = nullptr;
            size_t count_ = 0;
        };

    } // namespace Test
} // namespace SushiRuntime
