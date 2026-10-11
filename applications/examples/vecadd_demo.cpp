/**************************************************************************/
/* vecadd_demo.cpp                                                        */
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
 * @file vecadd_demo.cpp
 * @brief Runs C = A + B over typed buffers, with two tasks ordered by the data they touch.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstdio>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Runs C = A + B and prints a sample of the result; returns 0 when it is correct. */
    int run_vecadd()
    {
        constexpr std::size_t N = 1u << 20;   // 1,048,576 elements

        auto rt = Runtime::create().value();

        auto a = rt.buffer<float>(N).value();
        auto b = rt.buffer<float>(N).value();
        auto c = rt.buffer<float>(N).value();

        auto g = rt.graph();

        // Out(a) and Out(b) register the writes and pass the kernel the element pointers.
        g.add(Extent{N}, Out(a), Out(b),
            [](sycl::id<1> id, float* a, float* b)
            {
                const std::size_t i = id[0];
                a[i] = static_cast<float>(i);
                b[i] = static_cast<float>(2 * i);
            });

        // Reading A and B, which the fill task wrote, orders this task after it.
        g.add(Extent{N}, In(a), In(b), Out(c),
            [](sycl::id<1> id, const float* a, const float* b, float* c)
            {
                const std::size_t i = id[0];
                c[i] = a[i] + b[i];
            });

        g.run();

        // Shared buffers are host-addressable once the run has completed.
        std::printf("vecadd: N = %zu\n", N);
        std::printf("  C[1]       = %.1f  (expected 3)\n",      c[1]);
        std::printf("  C[100]     = %.1f  (expected 300)\n",    c[100]);
        std::printf("  C[%zu] = %.1f  (expected %.1f)\n",
                    N - 1, c[N - 1], 3.0f * static_cast<float>(N - 1));

        bool ok = true;
        for (std::size_t i = 0; i < N; ++i)
            if (c[i] != static_cast<float>(3 * i)) { ok = false; break; }

        std::printf("  result: %s\n", ok ? "CORRECT" : "WRONG");
        return ok ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return run_vecadd();
    }
    catch (const SushiException& error)
    {
        std::fprintf(stderr, "vecadd: %s\n", error.what());
        return 1;
    }
}
