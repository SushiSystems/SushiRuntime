/**************************************************************************/
/* test_game_engine_example.cpp                                           */
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

// A worked example: driving a tiny game-engine frame loop on the runtime, end to
// end, with nothing but the public fluent API. It demonstrates the thesis that the
// task graph behaves like a game engine:
//
//   * Components are component-arrays (one Buffer per field: px, py, vx, vy, alive).
//   * Systems are graph nodes that declare which components they read and write
//     (In/Out/InOut). The dependency tracker — not any hand-written schedule — orders
//     them: gravity writes velocity, integrate reads it, collision rewrites it,
//     culling reads the new position. Non-conflicting work across the 4096 entities
//     runs in parallel automatically.
//   * The frame is compiled once and replayed every step (run(STEPS)); no per-frame
//     rebuild.
//   * Despawn is deferred: the cull system writes `alive` only after every physics
//     system has read it that step (a write-after-read edge), so a kill takes effect
//     on the next frame, never mid-frame — the command-buffer discipline an ECS needs.
//
// The whole simulation is checked against an independent scalar reference running the
// same four systems in the same order, so "it behaves like a game engine" is a
// verified claim, not a vibe.

#include <cmath>
#include <cstddef>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N     = 4096;   // entities
    constexpr std::size_t STEPS = 600;    // frames
    constexpr float DT    = 0.01f;        // seconds per frame
    constexpr float G     = -9.8f;        // gravity (down)
    constexpr float REST  = 0.5f;         // floor restitution
    // An off-grid cull threshold: no entity's x ( = vx * 0.01 * step ) lands on it at
    // an integer step, so the crossing frame is identical on host and device and the
    // frozen-at-cull position never differs by a whole step's worth of motion.
    constexpr float XMAX  = 30.37f;

    // Horizontal speed per entity. Final x = vx * (STEPS * DT) = vx * 6 = one of
    // {12,24,36,48,60,72}; the first two survive (< XMAX), the rest are culled — every
    // value clears the threshold by a wide margin, so the decision is unambiguous.
    float vx_of(std::size_t i) { return 2.0f + static_cast<float>(i % 6) * 2.0f; }
}

TEST(Integration_GameEngineExample, ParticleWorldMatchesReference)
{
    auto rt = Runtime::create();

    // --- Component arrays --------------------------------------------------
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

    // --- Systems: declared by the components they touch; ordered automatically ---
    auto world = rt.graph();

    // gravity: accelerate living entities downward. The frame constants are captured
    // by value so the kernel carries them directly.
    world.add(Extent{N}, InOut(vy), In(alive),
        [g = G, dt = DT](sycl::id<1> id, float* vy, const int* alive)
        {
            const std::size_t i = id[0];
            if (alive[i]) vy[i] += g * dt;
        });

    // integrate: advance position by velocity (reads the velocity gravity just wrote).
    world.add(Extent{N}, In(vx), In(vy), InOut(px), InOut(py), In(alive),
        [dt = DT](sycl::id<1> id, const float* vx, const float* vy, float* px, float* py, const int* alive)
        {
            const std::size_t i = id[0];
            if (alive[i]) { px[i] += vx[i] * dt; py[i] += vy[i] * dt; }
        });

    // collide: bounce off the floor (reads the position integrate just wrote).
    world.add(Extent{N}, InOut(py), InOut(vy), In(alive),
        [rest = REST](sycl::id<1> id, float* py, float* vy, const int* alive)
        {
            const std::size_t i = id[0];
            if (alive[i] && py[i] < 0.0f) { py[i] = 0.0f; vy[i] = -vy[i] * rest; }
        });

    // cull: despawn entities that drifted out of bounds. It writes `alive` after the
    // three systems above have read it, so the kill lands on the next frame.
    world.add(Extent{N}, In(px), InOut(alive),
        [xmax = XMAX](sycl::id<1> id, const float* px, int* alive)
        {
            const std::size_t i = id[0];
            if (alive[i] && px[i] > xmax) alive[i] = 0;
        });

    world.run(STEPS);

    // Compiled once, replayed every frame — the game-engine property.
    EXPECT_EQ(world.compile_count(), 1u);

    // --- Independent scalar reference: same four systems, same order ----------
    std::vector<float> rpx(N), rpy(N), rvx(N), rvy(N);
    std::vector<int>   ralive(N);
    for (std::size_t i = 0; i < N; ++i)
    { rpx[i] = 0.0f; rpy[i] = 10.0f; rvx[i] = vx_of(i); rvy[i] = 0.0f; ralive[i] = 1; }

    for (std::size_t s = 0; s < STEPS; ++s)
        for (std::size_t i = 0; i < N; ++i)
        {
            if (ralive[i]) rvy[i] += G * DT;
            if (ralive[i]) { rpx[i] += rvx[i] * DT; rpy[i] += rvy[i] * DT; }
            if (ralive[i] && rpy[i] < 0.0f) { rpy[i] = 0.0f; rvy[i] = -rvy[i] * REST; }
            if (ralive[i] && rpx[i] > XMAX) ralive[i] = 0;
        }

    // --- Verify ---------------------------------------------------------------
    std::size_t alive_count = 0, culled_count = 0;
    for (std::size_t i = 0; i < N; ++i)
    {
        // Despawn decisions must match the reference exactly.
        ASSERT_EQ(alive[i], ralive[i]) << "alive mismatch at entity " << i;
        if (alive[i]) ++alive_count; else ++culled_count;

        // Horizontal motion is pure linear advection: it should track the reference
        // tightly on any device.
        EXPECT_NEAR(px[i], rpx[i], 0.05f) << "px drift at entity " << i;

        // The floor is a hard invariant for everyone — no entity ends below it.
        EXPECT_GE(py[i], -1e-3f) << "entity " << i << " fell through the floor";
    }

    // The cull system did real work: some entities survived, some were despawned.
    EXPECT_GT(alive_count, 0u);
    EXPECT_GT(culled_count, 0u);
    EXPECT_EQ(alive_count + culled_count, N);
}
