/**************************************************************************/
/* fluent_quickstart.cpp                                                  */
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
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/**************************************************************************/

// Quick start for the fluent v1 API. One include, a runtime, a chained graph,
// and a non-throwing run. Build with any supported SYCL toolchain.

#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

int main()
{
    auto rt = Runtime::create();   // default device selection
    auto g  = rt.graph();

    // Per-element task (auto parallel_for) chained with a raw-handler kernel.
    g.add(1024, [](std::size_t i)   { (void)i; })
     .add([](sycl::handler& h)      { h.single_task([]() {}); });

    auto report = g.try_run();     // std::expected<RunReport, Error>
    if (!report)
    {
        std::fprintf(stderr, "run failed: %s (%s)\n",
                     report.error().message.c_str(),
                     Core::to_string(report.error().code));
        return 1;
    }

    std::printf("ok: %zu tasks in %.3f ms (analysis %.3f ms)\n",
                static_cast<size_t>(report->total_tasks_executed),
                report->total_duration_ms,
                report->graph_analysis_duration_ms);
    return 0;
}
