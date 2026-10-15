/**************************************************************************/
/* test_order_recorder.cpp                                                */
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
 * @file test_order_recorder.cpp
 * @brief Tests make_order_recorder(): the callable appends its id under the mutex.
 * @see tests/engine/probe_tasks/README.md
 * @author Mustafa Garip
 */

#include <mutex>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/probe_tasks/order_recorder.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Tests that each recorder appends its own id, in the order they are called. */
TEST(Medium_OrderRecorder, AppendsItsIdWhenCalled)
{
    std::vector<int> order;
    std::mutex order_mutex;
    auto first = make_order_recorder(1, order, order_mutex);
    auto second = make_order_recorder(2, order, order_mutex);
    sycl::queue queue;
    (void)second(queue, {});
    (void)first(queue, {});
    (void)second(queue, {});
    EXPECT_EQ(order, (std::vector<int>{2, 1, 2}));
}
