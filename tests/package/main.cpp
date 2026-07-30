/**************************************************************************/
/* main.cpp                                                               */
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

// The smallest thing that proves the package works: one kernel, recorded and
// run against an installed SushiRuntime, from a project that has no access to
// the runtime's source tree.
//
// It is small on purpose. Anything more would be testing the runtime, which the
// runtime's own suite does; what is under test here is the package — that the
// headers are found, that the kernel template compiles in THIS project's
// translation unit under the toolchain the consumer chose, that the library
// links, and that the version the headers describe is the version that loaded.

#include <cstddef>
#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

int main()
{
    using namespace SushiRuntime;

    // The check every consumer should make once at startup. Headers and binary
    // coming from different releases is the classic packaging failure, and it
    // otherwise surfaces as a crash somewhere unrelated and much later.
    if (!version_matches())
    {
        std::printf("FAIL: headers are %s, loaded library is %s\n",
                    SUSHIRUNTIME_VERSION_STRING, runtime_version_string());
        return 1;
    }

    constexpr std::size_t N = 1024;

    auto runtime = Runtime::create();
    auto buffer  = runtime.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buffer[i] = 0;

    int* data = buffer.data();

    auto graph = runtime.graph();
    graph.add(API::Reads{}, API::Writes(buffer), N,
              [data](std::size_t i) { data[i] = static_cast<int>(i) + 1; });

    const Core::RunReport report = graph.run();
    if (!report.is_successful)
    {
        std::printf("FAIL: the run reported failure\n");
        return 1;
    }

    for (std::size_t i = 0; i < N; ++i)
    {
        if (buffer[i] != static_cast<int>(i) + 1)
        {
            std::printf("FAIL: element %zu is %d, expected %zu\n",
                        i, buffer[i], i + 1);
            return 1;
        }
    }

    std::printf("OK: SushiRuntime %s ran %zu tasks in %.3f ms\n",
                runtime_version_string(),
                report.total_tasks_executed,
                report.total_duration_ms);
    return 0;
}
