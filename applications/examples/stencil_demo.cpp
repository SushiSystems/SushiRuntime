/**************************************************************************/
/* stencil_demo.cpp                                                       */
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
 * @file stencil_demo.cpp
 * @brief Runs a 2-D heat-diffusion stencil that updates only the interior of a State.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstdio>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Runs the 2-D diffusion over the interior and prints its total heat; returns 0. */
    int run_stencil()
    {
        constexpr std::size_t W = 64, H = 64, K_STEPS = 200;
        constexpr float ALPHA = 0.2f;   // diffusion factor, stable for a 5-point stencil

        auto rt   = Runtime::create().value();
        auto temp = rt.state<float>(W * H).value();

        // Both buffers are seeded, because the interior-only update never writes the border.
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

        // The extent covers the (W-2) x (H-2) interior and offsets the kernel's index to (1,1).
        Extent<2> interior{ sycl::range<2>(W - 2, H - 2), sycl::id<2>(1, 1) };
        g.add_rule(interior, temp, [](sycl::id<2> idx, const float* cur, float* next)
        {
            const std::size_t x = idx[0];
            const std::size_t y = idx[1];
            const std::size_t c = y * W + x;
            const float lap = cur[c - 1] + cur[c + 1] + cur[c - W] + cur[c + W]
                              - 4.0f * cur[c];
            next[c] = cur[c] + ALPHA * lap;
        });

        const auto report = g.run(K_STEPS).value();

        // The total heat of a closed diffusion is conserved; report it as a smoke check.
        double total = 0.0;
        for (std::size_t i = 0; i < W * H; ++i)
            total += temp[i];

        std::printf("stencil: %zux%zu, %zu steps in %.3f ms; total heat = %.3f\n",
                    W, H, K_STEPS, report.total_duration_ms, total);
        return 0;
    }
} // namespace

int main()
{
    return run_stencil();
}
