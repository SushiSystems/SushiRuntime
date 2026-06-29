/**************************************************************************/
/* vecadd_demo.cpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// The "hello, dependencies" example: C = A + B over typed buffers, with the two
// tasks ordered automatically by the data they touch.
//
// Three buffers are allocated as Buffer<float> handles. A first task fills A and
// B (it writes them); a second task reads A and B and writes C. Because the add
// task reads what the fill task wrote, the runtime infers a read-after-write edge
// and runs them in order — no dependency is wired by hand. In(...)/Out(...) wrap a
// buffer so add() both registers the access for the tracker and hands the kernel
// the raw element pointer as an argument.

#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

int main()
{
    constexpr std::size_t N = 1u << 20;   // 1,048,576 elements

    auto rt = Runtime::create();

    auto a = rt.buffer<float>(N);
    auto b = rt.buffer<float>(N);
    auto c = rt.buffer<float>(N);

    auto g = rt.graph();

    // Fill A and B on the device. Out(a)/Out(b) register the writes and pass the
    // element pointers, so the kernel writes through them directly.
    g.add(Extent{N}, Out(a), Out(b),
        [](sycl::id<1> id, float* a, float* b)
        {
            const std::size_t i = id[0];
            a[i] = static_cast<float>(i);
            b[i] = static_cast<float>(2 * i);
        });

    // C = A + B. Reading A and B (which the fill task wrote) is what orders this
    // task after it. The runtime serializes only this true dependency.
    g.add(Extent{N}, In(a), In(b), Out(c),
        [](sycl::id<1> id, const float* a, const float* b, float* c)
        {
            const std::size_t i = id[0];
            c[i] = a[i] + b[i];
        });

    g.run();

    // Read a couple of values back. Shared buffers are host-addressable, so the
    // host can index them directly once the run has completed.
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
