/**************************************************************************/
/* quickstart.cpp                                                         */
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
 * @file quickstart.cpp
 * @brief Runs a 1-D three-point diffusion stencil over a State for a fixed number of steps.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstdio>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Runs the diffusion stencil and prints its total heat; returns 0 on success. */
    int run_quickstart()
    {
        constexpr std::size_t N     = 1024;
        constexpr std::size_t STEPS = 10000;

        auto rt = Runtime::create().value();          // discovers hardware, starts the workers

        // The runtime keeps two buffers and swaps them; a rule reads one, writes the other.
        auto heat = rt.state<float>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            heat[i] = (i == N / 2) ? 1.0f : 0.0f;   // a single hot cell in the middle

        auto sim = rt.graph();

        // fn receives the index and the current and next buffers; the runtime tracks the access.
        sim.add(heat, [](std::size_t i, const float* cur, float* next)
        {
            const float left  = (i == 0)     ? cur[i] : cur[i - 1];
            const float right = (i == N - 1) ? cur[i] : cur[i + 1];
            next[i] = cur[i] + 0.25f * (left - 2.0f * cur[i] + right);
        });

        // The graph compiles once and replays STEPS times.
        const auto report = sim.run(STEPS).value();

        // Heat is conserved by this closed diffusion; report it as a smoke check.
        double total = 0.0;
        for (std::size_t i = 0; i < N; ++i)
            total += heat[i];

        std::printf("quickstart: %zu steps in %.2f ms; total heat = %.4f (compiles = %zu)\n",
                    STEPS, report.total_duration_ms, total, sim.compile_count());
        return 0;
    }
} // namespace

int main()
{
    return run_quickstart();
}
