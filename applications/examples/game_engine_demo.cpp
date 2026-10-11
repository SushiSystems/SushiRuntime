/**************************************************************************/
/* game_engine_demo.cpp                                                   */
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
 * @file game_engine_demo.cpp
 * @brief Runs an ECS-style frame loop of four systems and checks it against a scalar reference.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N     = 4096;   // entities
    constexpr std::size_t K_STEPS = 600;    // frames
    constexpr float DT    = 0.01f;        // seconds per frame
    constexpr float G     = -9.8f;        // gravity (down)
    constexpr float REST  = 0.5f;         // floor restitution
    // XMAX lies off every x an entity reaches at integer steps (applications/examples/README.md).
    constexpr float XMAX  = 30.37f;

    // Final x is vx * 6, one of {12,24,36,48,60,72}; the first two stay under XMAX.
    float vx_of(std::size_t i) { return 2.0f + static_cast<float>(i % 6) * 2.0f; }

    /** @brief Runs the four systems over N entities and checks them against a reference. */
    int run_game_engine()
    {
        auto rt = Runtime::create();

        // Component arrays.
        auto px = rt.buffer<float>(N);
        auto py = rt.buffer<float>(N);
        auto vx = rt.buffer<float>(N);
        auto vy = rt.buffer<float>(N);
        auto alive = rt.buffer<int>(N);

        for (std::size_t i = 0; i < N; ++i)
        {
            px[i] = 0.0f;
            py[i] = 10.0f;          // start above the floor
            vx[i] = vx_of(i);
            vy[i] = 0.0f;
            alive[i] = 1;
        }

        // Systems: declared by the components they touch; ordered automatically.
        auto world = rt.graph();

        // gravity: accelerate living entities downward.
        world.add(Extent{N}, InOut(vy), In(alive),
            [g = G, dt = DT](sycl::id<1> id, float* vy, const int* alive)
            {
                const std::size_t i = id[0];
                if (alive[i]) vy[i] += g * dt;
            }).name("gravity");

        // integrate: advance position by velocity (reads the velocity gravity just wrote).
        world.add(Extent{N}, In(vx), In(vy), InOut(px), InOut(py), In(alive),
            [dt = DT](sycl::id<1> id, const float* vx, const float* vy, float* px, float* py,
                      const int* alive)
            {
                const std::size_t i = id[0];
                if (alive[i]) { px[i] += vx[i] * dt; py[i] += vy[i] * dt; }
            }).name("integrate");

        // collide: bounce off the floor (reads the position integrate just wrote).
        world.add(Extent{N}, InOut(py), InOut(vy), In(alive),
            [rest = REST](sycl::id<1> id, float* py, float* vy, const int* alive)
            {
                const std::size_t i = id[0];
                if (alive[i] && py[i] < 0.0f) { py[i] = 0.0f; vy[i] = -vy[i] * rest; }
            }).name("collide");

        // cull writes `alive` after the three systems above read it, so a kill lands next frame.
        world.add(Extent{N}, In(px), InOut(alive),
            [xmax = XMAX](sycl::id<1> id, const float* px, int* alive)
            {
                const std::size_t i = id[0];
                if (alive[i] && px[i] > xmax) alive[i] = 0;
            }).name("cull");

        const auto report = world.run(K_STEPS);

        // Independent scalar reference: same four systems, same order.
        std::vector<float> rpx(N), rpy(N), rvx(N), rvy(N);
        std::vector<int>   ralive(N);
        for (std::size_t i = 0; i < N; ++i)
        { rpx[i] = 0.0f; rpy[i] = 10.0f; rvx[i] = vx_of(i); rvy[i] = 0.0f; ralive[i] = 1; }

        for (std::size_t s = 0; s < K_STEPS; ++s)
            for (std::size_t i = 0; i < N; ++i)
            {
                if (ralive[i]) rvy[i] += G * DT;
                if (ralive[i]) { rpx[i] += rvx[i] * DT; rpy[i] += rvy[i] * DT; }
                if (ralive[i] && rpy[i] < 0.0f) { rpy[i] = 0.0f; rvy[i] = -rvy[i] * REST; }
                if (ralive[i] && rpx[i] > XMAX) ralive[i] = 0;
            }

        // Verify against the reference.
        std::size_t alive_count = 0, culled_count = 0;
        bool ok = true;
        for (std::size_t i = 0; i < N; ++i)
        {
            if (alive[i] != ralive[i]) ok = false;           // despawn decisions must match
            if (std::fabs(px[i] - rpx[i]) > 0.05f) ok = false; // horizontal motion tracks
            if (py[i] < -1e-3f) ok = false;                  // floor is a hard invariant
            if (alive[i]) ++alive_count; else ++culled_count;
        }

        std::printf("game engine: %zu entities, %zu frames in %.2f ms (compiles = %zu)\n",
                    N, K_STEPS, report.total_duration_ms, world.compile_count());
        std::printf("  alive = %zu, culled = %zu\n", alive_count, culled_count);
        std::printf("  result: %s\n", ok ? "CORRECT" : "WRONG");
        return ok ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return run_game_engine();
    }
    catch (const SushiException& error)
    {
        std::fprintf(stderr, "game_engine: %s\n", error.what());
        return 1;
    }
}
