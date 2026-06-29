/**************************************************************************/
/* streaming_world_demo.cpp                                               */
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

// A graph whose *set* of work changes between steps — the open-world streaming
// case. rt.dynamic_graph() partitions work into regions keyed by a caller id (here
// a tile index). region(k) records tasks with the same add() surface as a static
// graph; drop(k) removes one. Mutations take effect at the next step boundary, and
// only the regions that changed are rebuilt — an unchanged tile is never recompiled.
//
// The world below holds a grid of tiles, of which only a window is active at once.
// First the window is tiles {0,1,2,3}; then it slides to {2,3,4,5}: tiles 0 and 1
// stream out, 4 and 5 stream in, and 2 and 3 stay live across the recomposition.
// Each active tile's task increments every cell once per step it is active, so the
// final cell value is exactly the number of steps that tile was in view.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

int main()
{
    constexpr std::size_t TILES  = 8;
    constexpr std::size_t CELL_N = 4096;
    constexpr std::size_t STEPS  = 50;

    auto rt = Runtime::create();

    // One buffer per tile, allocated up front so every handle outlives the graph.
    std::vector<Buffer<int>> tile;
    tile.reserve(TILES);
    for (std::size_t k = 0; k < TILES; ++k)
    {
        auto buf = rt.buffer<int>(CELL_N);
        for (std::size_t i = 0; i < CELL_N; ++i) buf[i] = 0;
        tile.push_back(std::move(buf));
    }

    auto world = rt.dynamic_graph();

    // Records the per-step "increment every cell" task on tile k's region.
    auto activate = [&](std::uint64_t k)
    {
        int* p = tile[k].data();
        world.region(k).add(Reads(), Writes(tile[k]), CELL_N,
                            [p](std::size_t i) { p[i] += 1; });
    };

    // Window 1: tiles {0,1,2,3} in view.
    for (std::uint64_t k = 0; k < 4; ++k) activate(k);
    world.run(STEPS);

    // Slide the window to {2,3,4,5}: stream 0 and 1 out, 4 and 5 in. Tiles 2 and 3
    // are untouched and are not rebuilt.
    world.drop(0);
    world.drop(1);
    activate(4);
    activate(5);
    world.run(STEPS);

    std::printf("streaming world: %zu tiles, 2 windows of %zu steps\n", TILES, STEPS);
    std::printf("  live regions = %zu, compositions = %zu\n",
                world.region_count(), world.compile_count());
    for (std::size_t k = 0; k < TILES; ++k)
        std::printf("  tile %zu: cell[0] = %d\n", k, tile[k][0]);

    // tiles {0,1}: one window -> STEPS; {2,3}: both windows -> 2*STEPS;
    // {4,5}: one window -> STEPS; {6,7}: never active -> 0.
    const bool ok =
        tile[0][0] == static_cast<int>(STEPS)     && tile[1][0] == static_cast<int>(STEPS) &&
        tile[2][0] == static_cast<int>(2 * STEPS) && tile[3][0] == static_cast<int>(2 * STEPS) &&
        tile[4][0] == static_cast<int>(STEPS)     && tile[5][0] == static_cast<int>(STEPS) &&
        tile[6][0] == 0                           && tile[7][0] == 0;

    std::printf("  result: %s\n", ok ? "CORRECT" : "WRONG");
    return ok ? 0 : 1;
}
