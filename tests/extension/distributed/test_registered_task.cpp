/**************************************************************************/
/* test_registered_task.cpp                                               */
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
 * @file test_registered_task.cpp
 * @brief Tests record_registered_task(): the recorded node runs the registered kernel on this machine.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/registered_task.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that a run with no cluster calls the registered kernel with the task's views. */
TEST(Medium_RegisteredTask, TheKernelRunsLocally)
{
    constexpr std::size_t K_COUNT = 8;
    auto runtime = Runtime::create().value();
    auto in = runtime.buffer<float>(K_COUNT).value();
    auto out = runtime.buffer<float>(K_COUNT).value();
    for (std::size_t index = 0; index < K_COUNT; ++index)
    {
        in.data()[index] = static_cast<float>(index);
        out.data()[index] = -1.0f;
    }

    const OperationID op(77);
    sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
    kernels->register_kernel(op, [](sycl::queue& queue, span<const BufferView> inputs,
                                    span<const BufferView> outputs, const NodeOptions&)
    {
        const float* source = inputs[0].as<const float>();
        float* target = outputs[0].as<float>();
        const std::size_t count = outputs[0].size_bytes / sizeof(float);
        return queue.parallel_for(sycl::range<1>(count), [=](sycl::id<1> index)
        {
            target[index[0]] = source[index[0]] * 2.0f;
        });
    });

    NodeOptions options;
    options.operation = op;
    const BufferView inputs[1] = {BufferView{in.data(), K_COUNT * sizeof(float)}};
    const BufferView outputs[1] = {BufferView{out.data(), K_COUNT * sizeof(float)}};
    auto graph = runtime.graph();
    graph.extend([&](Recorder& recorder)
    {
        record_registered_task(recorder, *kernels, options, span<const BufferView>(inputs, 1),
                               span<const BufferView>(outputs, 1));
    });
    graph.run().value();
    for (std::size_t index = 0; index < K_COUNT; ++index)
        EXPECT_EQ(out.data()[index], 2.0f * static_cast<float>(index)) << "at " << index;
}
