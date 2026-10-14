/**************************************************************************/
/* order_recorder.hpp                                                     */
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
 * @file order_recorder.hpp
 * @brief Declares the host task that records its ID into a shared list.
 * @author Mustafa Garip
 */

#pragma once

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Test
    {
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
    } // namespace Test
} // namespace SushiRuntime
