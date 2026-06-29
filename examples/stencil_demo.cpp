/**************************************************************************/
/* stencil_demo.cpp                                                       */
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

// A 2-D heat-diffusion stencil written entirely against the fluent API: a
// double-buffered State, a multidimensional Extent that updates only the interior
// (the fixed Dirichlet border is the halo it skips), and a stepped run. No raw
// sycl::handler, no manual ping-pong.

#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

int main()
{
    constexpr std::size_t W = 64, H = 64, STEPS = 200;
    constexpr float ALPHA = 0.2f;   // diffusion factor, stable for a 5-point stencil

    auto rt   = Runtime::create();
    auto temp = rt.state<float>(W * H);

    // Seed both fields identically: a cold plate with a hot interior block. Both
    // buffers are seeded because the interior-only update never writes the border,
    // so the border must already hold its fixed boundary value in each field.
    float* cur0  = temp.current();
    float* next0 = temp.next();
    for (std::size_t y = 0; y < H; ++y)
    {
        for (std::size_t x = 0; x < W; ++x)
        {
            const bool hot = (x > W / 3 && x < 2 * W / 3 && y > H / 3 && y < 2 * H / 3);
            const float v = hot ? 1.0f : 0.0f;
            cur0[y * W + x]  = v;
            next0[y * W + x] = v;
        }
    }

    auto g = rt.graph();

    // Update only the (W-2) x (H-2) interior; the one-cell border is the halo and
    // stays fixed. The kernel receives the global 2-D index already offset to (1,1).
    Extent<2> interior{ sycl::range<2>(W - 2, H - 2), sycl::id<2>(1, 1) };
    g.add(temp, interior, [](sycl::id<2> idx, const float* cur, float* next)
    {
        const std::size_t x = idx[0];
        const std::size_t y = idx[1];
        const std::size_t c = y * W + x;
        const float lap = cur[c - 1] + cur[c + 1] + cur[c - W] + cur[c + W]
                          - 4.0f * cur[c];
        next[c] = cur[c] + ALPHA * lap;
    });

    const auto report = g.run(STEPS);

    // The total heat of a closed diffusion is conserved; report it as a smoke check.
    double total = 0.0;
    for (std::size_t i = 0; i < W * H; ++i)
        total += temp[i];

    std::printf("stencil: %zux%zu, %zu steps in %.3f ms; total heat = %.3f\n",
                W, H, STEPS, report.total_duration_ms, total);
    return 0;
}
