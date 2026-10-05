/****************************************************************/
/* game_engine_demo.cpp                                         */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// A tiny game-engine frame loop driven end to end on the runtime, with nothing but
// the public fluent API. It shows that the task graph behaves like a game engine:
//
//   * Components are component-arrays (one Buffer per field: px, py, vx, vy, alive).
//   * Systems are graph nodes that declare which components they read and write
//     (In/Out/InOut). The dependency tracker — not any hand-written schedule —
//     orders them: gravity writes velocity, integrate reads it, collision rewrites
//     it, culling reads the new position. Non-conflicting work across the entities
//     runs in parallel automatically.
//   * The frame is compiled once and replayed every step (run(STEPS)); no per-frame
//     rebuild.
//   * Despawn is deferred: the cull system writes `alive` only after every physics
//     system has read it that step (a write-after-read edge), so a kill takes effect
//     on the next frame, never mid-frame — the command-buffer discipline an ECS needs.
//
// The result is checked against an independent scalar reference running the same
// four systems in the same order.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

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
    // {12,24,36,48,60,72}; the first two survive (< XMAX), the rest are culled.
    float vx_of(std::size_t i) { return 2.0f + static_cast<float>(i % 6) * 2.0f; }
}

int main()
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

    const auto report = world.run(STEPS);

    // Independent scalar reference: same four systems, same order.
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
                N, STEPS, report.total_duration_ms, world.compile_count());
    std::printf("  alive = %zu, culled = %zu\n", alive_count, culled_count);
    std::printf("  result: %s\n", ok ? "CORRECT" : "WRONG");
    return ok ? 0 : 1;
}
