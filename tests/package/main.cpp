/****************************************************************/
/* main.cpp                                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file main.cpp
 * @brief Runs one kernel against an installed SushiRuntime and checks the loaded version.
 * @author Mustafa Garip
 * @see tests/package/README.md
 */

#include <cstddef>
#include <cstdio>

#include <SushiRuntime/SushiRuntime.h>

int main()
{
    using namespace SushiRuntime;

    // Headers and library must come from one release; see tests/package/README.md.
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
