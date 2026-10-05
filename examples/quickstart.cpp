/****************************************************************/
/* quickstart.cpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// The shortest complete SushiRuntime program: one include, a runtime, a typed
// State, a single update rule, and a stepped run. It evolves a 1-D field with a
// three-point diffusion stencil. The runtime double-buffers the State, supplies
// the current/next pointers to the rule, compiles the graph once, and replays it
// for every step. No sycl::handler, no manual buffer swap, no hand-wired
// dependencies.
//
// Read this first, then move on to the focused *_demo.cpp examples:
//   vecadd_demo.cpp          - buffers and automatic dependency ordering
//   stencil_demo.cpp         - a 2-D grid with an interior-only (halo) update
//   coupled_fields_demo.cpp  - several fields evolving together (Jacobi)
//   game_engine_demo.cpp     - an ECS-style frame loop built from the data flow
//   streaming_world_demo.cpp - a graph whose set of work changes between steps
//   distributed_demo.cpp     - offloading a task to a remote worker (opt-in)

#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

int main()
{
    constexpr std::size_t N     = 1024;
    constexpr std::size_t STEPS = 10000;

    auto rt = Runtime::create();          // discovers hardware, starts the workers

    // A time-evolving field. The runtime keeps two buffers and swaps them each
    // step, so a rule reads the current field and writes the next.
    auto heat = rt.state<float>(N);
    for (std::size_t i = 0; i < N; ++i)
        heat[i] = (i == N / 2) ? 1.0f : 0.0f;   // a single hot cell in the middle

    auto sim = rt.graph();

    // One update rule. fn receives the index plus the current and next buffers;
    // the runtime supplies the pointers and tracks the read/write for you.
    sim.add(heat, [](std::size_t i, const float* cur, float* next)
    {
        const float left  = (i == 0)     ? cur[i] : cur[i - 1];
        const float right = (i == N - 1) ? cur[i] : cur[i + 1];
        next[i] = cur[i] + 0.25f * (left - 2.0f * cur[i] + right);
    });

    // Compile once, replay STEPS times. The worker pool, device dispatch, and
    // buffer swapping are all handled for you.
    const auto report = sim.run(STEPS);

    // Heat is conserved by this closed diffusion; report it as a smoke check.
    double total = 0.0;
    for (std::size_t i = 0; i < N; ++i)
        total += heat[i];

    std::printf("quickstart: %zu steps in %.2f ms; total heat = %.4f (compiles = %zu)\n",
                STEPS, report.total_duration_ms, total, sim.compile_count());
    return 0;
}
