/**************************************************************************/
/* stepping_demo.cpp                                                      */
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
 * @file stepping_demo.cpp
 * @brief Runs one rule through every run entry, fed by a hook before each step.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Cells of the field. */
    constexpr std::size_t K_CELLS = 256;

    /** @brief Steps of the run with a fixed count. */
    constexpr std::size_t K_FIXED_STEPS = 4;

    /** @brief Cell value at which run_until() stops. */
    constexpr std::uint32_t K_THRESHOLD = 25;

    /** @brief Wall-clock budget of run_for(). */
    constexpr std::chrono::milliseconds K_BUDGET{2};

    /** @brief Prints what the report of one run entry holds, and the cell after it. */
    void print_run(const char* entry, const RunReport& report, std::uint32_t cell)
    {
        std::printf("  %-10s tasks = %zu, successful = %s, cancelled = %s, cell = %u\n", entry,
                    report.total_tasks_executed, report.is_successful ? "yes" : "no",
                    report.cancelled ? "yes" : "no", cell);
    }

    /** @brief Steps a field with each run entry and checks it against a count kept on the host. */
    int run_stepping_demo()
    {
        auto runtime = Runtime::create().value();
        auto gain = runtime.buffer<std::uint32_t>(1).value();
        auto field = runtime.state<std::uint32_t>(K_CELLS).value();
        for (std::size_t i = 0; i < K_CELLS; ++i)
            field[i] = 0;
        gain[0] = 0;

        const std::uint32_t* g = gain.data();
        auto graph = runtime.graph();
        graph.add_rule(field, [g](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
        {
            next[i] = current[i] + g[0];
        });

        // The hook writes the word the kernel of the coming step reads; no plan is rebuilt.
        std::uint32_t expected = 0;
        std::size_t steps = 0;
        graph.before_each_step([&gain, &expected, &steps](std::size_t finished_steps)
        {
            gain[0] = static_cast<std::uint32_t>(finished_steps) + 1u;
            expected += gain[0];
            ++steps;
        });

        std::printf("stepping: %zu cells; a step adds 1 + the steps its run finished\n", K_CELLS);

        const RunReport fixed = graph.run(K_FIXED_STEPS).value();
        print_run("run(4)", fixed, field[0]);
        bool is_correct = fixed.total_tasks_executed == K_FIXED_STEPS && field[0] == 10u;

        // The stop test runs before each step and reads the field the last step advanced to.
        const RunReport until =
            graph.run_until([&field] { return field[0] >= K_THRESHOLD; }).value();
        print_run("run_until", until, field[0]);
        is_correct = is_correct && field[0] == K_THRESHOLD && steps == K_FIXED_STEPS + 5;

        // run_async() returns once the step is on the worker pool; wait() ends the step.
        auto started = graph.run_async();
        if (!started)
        {
            std::fprintf(stderr, "stepping_demo: run_async() failed with %s: %s\n",
                         to_string(started.error().code), started.error().message.c_str());
            return 1;
        }
        RunHandle<Graph> step = std::move(started).value();
        const bool was_pending = step.pending();
        const RunReport overlapped = step.wait().value();
        print_run("run_async", overlapped, field[0]);
        is_correct = is_correct && was_pending && !step.pending() && field[0] == K_THRESHOLD + 1;

        // How many steps fit the budget depends on the machine, so the count is not compared.
        const std::size_t steps_before = steps;
        const RunReport timed = graph.run_for(K_BUDGET).value();
        std::printf("  run_for    %zu steps in a budget of 2 ms (depends on the machine)\n",
                    steps - steps_before);
        is_correct = is_correct && timed.is_successful && steps > steps_before;

        for (std::size_t i = 0; i < K_CELLS; ++i)
            is_correct = is_correct && field[i] == expected;
        is_correct = is_correct && graph.compile_count() == 1;

        std::printf("  the field matches the host count: %s (compiles = %zu)\n",
                    field[0] == expected ? "yes" : "no", graph.compile_count());
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_stepping_demo();
}
