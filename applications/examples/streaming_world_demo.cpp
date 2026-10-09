/**************************************************************************/
/* streaming_world_demo.cpp                                               */
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
 * @file streaming_world_demo.cpp
 * @brief Runs a dynamic graph whose window of active tiles slides between two runs.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

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

    // Tiles 0 and 1 stream out, 4 and 5 in; tiles 2 and 3 are not rebuilt.
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

    // A tile's cell counts the steps it was in view: STEPS, 2 * STEPS or 0.
    const bool ok =
        tile[0][0] == static_cast<int>(STEPS)     && tile[1][0] == static_cast<int>(STEPS) &&
        tile[2][0] == static_cast<int>(2 * STEPS) && tile[3][0] == static_cast<int>(2 * STEPS) &&
        tile[4][0] == static_cast<int>(STEPS)     && tile[5][0] == static_cast<int>(STEPS) &&
        tile[6][0] == 0                           && tile[7][0] == 0;

    std::printf("  result: %s\n", ok ? "CORRECT" : "WRONG");
    return ok ? 0 : 1;
}
