/**************************************************************************/
/* main.cpp                                                               */
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
 * @file main.cpp
 * @brief Runs the quickstart kernel against an installed SushiRuntime and checks its version.
 * @author Mustafa Garip
 * @see tests/package/README.md
 */

#include <cstddef>
#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

int main()
{
    // Headers and library must come from one release; see tests/package/README.md.
    if (!SushiRuntime::version_matches())
    {
        std::printf("FAIL: headers are %s, loaded library is %s\n",
                    SUSHIRUNTIME_VERSION_STRING, SushiRuntime::runtime_version_string());
        return 1;
    }

    constexpr std::size_t N     = 1024;
    constexpr std::size_t STEPS = 100;

    SushiRuntime::Runtime runtime = SushiRuntime::Runtime::create();

    SushiRuntime::State<float> heat = runtime.state<float>(N);
    for (std::size_t i = 0; i < N; ++i)
        heat[i] = (i == N / 2) ? 1.0f : 0.0f;

    SushiRuntime::API::Graph graph = runtime.graph();
    graph.add(heat, [](std::size_t i, const float* cur, float* next)
    {
        const float left  = (i == 0)     ? cur[i] : cur[i - 1];
        const float right = (i == N - 1) ? cur[i] : cur[i + 1];
        next[i] = cur[i] + 0.25f * (left - 2.0f * cur[i] + right);
    });

    const SushiRuntime::RunReport report = graph.run(STEPS);
    if (!report.is_successful)
    {
        std::printf("FAIL: the run reported failure\n");
        return 1;
    }

    // The diffusion is closed, so the heat the field started with is the heat it ends with.
    double total = 0.0;
    for (std::size_t i = 0; i < N; ++i)
        total += heat[i];

    if (total < 0.999 || total > 1.001)
    {
        std::printf("FAIL: total heat is %.6f, expected 1.0\n", total);
        return 1;
    }

    std::printf("OK: SushiRuntime %s ran %zu tasks in %.3f ms\n",
                SushiRuntime::runtime_version_string(),
                report.total_tasks_executed,
                report.total_duration_ms);
    return 0;
}
