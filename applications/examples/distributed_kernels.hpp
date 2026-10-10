/**************************************************************************/
/* distributed_kernels.hpp                                                */
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
 * @file distributed_kernels.hpp
 * @brief Declares the operations of the distributed demo and the kernels that run them.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/distributed/distributed.hpp>

/** @brief Holds what the master and the worker of the distributed demo must agree on. */
namespace DistributedDemo
{
    using SushiRuntime::operator""_op;

    /** @brief Names the operation that adds two float vectors element by element. */
    inline constexpr auto K_VECTOR_ADD = "demo.vector_add"_op;

    /** @brief Writes the sum of one element of @p a and @p b to the same element of @p sum. */
    inline void add_element(const float* a, const float* b, float* sum,
                            std::size_t index) noexcept
    {
        sum[index] = a[index] + b[index];
    }

    /**
     * @brief Runs K_VECTOR_ADD on the machine that received the task.
     * @param inputs The two addends, in the order the task lists them.
     * @param outputs The sum; its size gives the element count.
     */
    inline sycl::event add_vectors(
        sycl::queue& queue, SushiRuntime::span<const SushiRuntime::Distributed::BufferView> inputs,
        SushiRuntime::span<const SushiRuntime::Distributed::BufferView> outputs,
        const SushiRuntime::TaskMetadata&)
    {
        const float* a = inputs[0].as<const float>();
        const float* b = inputs[1].as<const float>();
        float* sum = outputs[0].as<float>();
        const std::size_t count = outputs[0].size_bytes / sizeof(float);

        return queue.parallel_for(sycl::range<1>(count), [a, b, sum](sycl::id<1> id)
        {
            add_element(a, b, sum, id[0]);
        });
    }

    /** @brief Registers the kernel of every operation of the demo. */
    inline void register_kernels(SushiRuntime::Distributed::KernelRegistry& kernels)
    {
        kernels.register_kernel(K_VECTOR_ADD, add_vectors);
    }
} // namespace DistributedDemo
