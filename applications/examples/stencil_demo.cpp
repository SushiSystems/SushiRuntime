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

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Width, height and steps of the plate. */
    constexpr std::size_t W = 64, H = 64, K_STEPS = 200;

    /** @brief Diffusion factor; a 5-point stencil is stable below 0.25. */
    constexpr float K_ALPHA = 0.2f;

    /** @brief Largest absolute difference accepted between a device cell and a host cell. */
    constexpr float K_TOLERANCE = 1.0e-4f;

    /** @brief Returns the plate after K_STEPS steps of the stencil, computed on the host. */
    std::vector<float> advance_on_host(std::vector<float> plate)
    {
        std::vector<float> next = plate;
        for (std::size_t step = 0; step < K_STEPS; ++step)
        {
            for (std::size_t y = 1; y + 1 < H; ++y)
            {
                for (std::size_t x = 1; x + 1 < W; ++x)
                {
                    const std::size_t c = y * W + x;
                    const float lap = plate[c - 1] + plate[c + 1] + plate[c - W] + plate[c + W]
                                      - 4.0f * plate[c];
                    next[c] = plate[c] + K_ALPHA * lap;
                }
            }
            plate.swap(next);
        }
        return plate;
    }

    /** @brief Runs the 2-D diffusion over the interior and checks it against the host. */
    int run_stencil()
    {
        // A cold plate with a hot block in the middle; the border stays cold.
        std::vector<float> initial(W * H, 0.0f);
        for (std::size_t y = H / 3 + 1; y < 2 * H / 3; ++y)
        {
            for (std::size_t x = W / 3 + 1; x < 2 * W / 3; ++x)
                initial[y * W + x] = 1.0f;
        }

        auto rt   = Runtime::create().value();
        auto temp = rt.state<float>(W * H).value();

        // Both buffers are seeded, because the interior-only update never writes the border.
        float* cur0  = temp.current();
        float* next0 = temp.next();
        for (std::size_t i = 0; i < W * H; ++i)
        {
            cur0[i]  = initial[i];
            next0[i] = initial[i];
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
            next[c] = cur[c] + K_ALPHA * lap;
        });

        const auto report = g.run(K_STEPS).value();

        const std::vector<float> expected = advance_on_host(initial);
        bool is_correct = true;
        double total = 0.0;
        double expected_total = 0.0;
        for (std::size_t i = 0; i < W * H; ++i)
        {
            is_correct = is_correct && std::fabs(temp[i] - expected[i]) <= K_TOLERANCE;
            total += temp[i];
            expected_total += expected[i];
        }

        std::printf("stencil: %zux%zu, %zu steps, %zu tasks\n", W, H, K_STEPS,
                    report.total_tasks_executed);
        std::printf("  total heat = %.1f  (expected %.1f)\n", total, expected_total);
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_stencil();
}
