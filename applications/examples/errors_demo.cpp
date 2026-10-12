/**************************************************************************/
/* errors_demo.cpp                                                        */
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
 * @file errors_demo.cpp
 * @brief Handles a failed allocation and a refused recording, then records and runs again.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Cells of the field the program settles for. */
    constexpr std::size_t K_CELLS = 64;

    /** @brief Steps of the run that succeeds. */
    constexpr std::size_t K_STEPS = 10;

    /** @brief Cells whose bytes fill half the address space, which no device grants. */
    constexpr std::size_t K_TOO_MANY_CELLS =
        std::numeric_limits<std::size_t>::max() / 2 / sizeof(std::uint32_t);

    /** @brief Prints the call that failed with the code and the message of its error. */
    void print_error(const char* call, const Error& error)
    {
        std::printf("  %s -> %s: %s\n", call, to_string(error.code), error.message.c_str());
    }

    /** @brief Records the rule that adds one to each of the first count cells of a field. */
    void record_increment(Graph& graph, State<std::uint32_t>& field, std::size_t count)
    {
        graph.add_rule(count, field,
                       [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
                       {
                           next[i] = current[i] + 1u;
                       });
    }

    /** @brief Walks the three kinds of failure a program handles; returns 0 when each behaved. */
    int run_errors_demo()
    {
        std::printf("errors:\n");

        // A program that can go on without the value tests the Result and leaves value() alone.
        auto created = Runtime::create();
        if (!created)
        {
            print_error("Runtime::create()", created.error());
            return 1;
        }
        const Runtime runtime = std::move(created).value();

        // A device that has no room returns AllocationFailed, and the program asks for less.
        const auto oversized = runtime.state<std::uint32_t>(K_TOO_MANY_CELLS);
        if (oversized || oversized.error().code != ErrorCode::AllocationFailed)
        {
            std::fprintf(stderr, "errors_demo: the oversized state was not refused\n");
            return 1;
        }
        std::printf("  state(too many cells) -> %s, so the program asks for %zu cells\n",
                    to_string(oversized.error().code), K_CELLS);
        auto field = runtime.state<std::uint32_t>(K_CELLS).value();
        for (std::size_t i = 0; i < K_CELLS; ++i)
            field[i] = 0;

        // A launch that reaches past its State is refused: nothing is recorded, the Error is kept.
        auto graph = runtime.graph();
        record_increment(graph, field, K_CELLS + 1);
        const Error* refusal = graph.refusal();
        if (refusal == nullptr)
        {
            std::fprintf(stderr, "errors_demo: the launch past the State was recorded\n");
            return 1;
        }
        print_error("refusal()", *refusal);

        // compile() returns the kept Error before any step runs, and so does run().
        const Result<void> compiled = graph.compile();
        const auto ran = graph.run(K_STEPS);
        const bool is_same_error = !compiled && !ran &&
                                   compiled.error().code == refusal->code &&
                                   compiled.error().message == refusal->message &&
                                   ran.error().code == refusal->code &&
                                   ran.error().message == refusal->message;
        std::printf("  compile() and run() return that error: %s (rules recorded = %zu)\n",
                    is_same_error ? "yes" : "no", graph.size());
        bool is_correct = is_same_error && graph.size() == 0 && field[0] == 0;

        // clear() forgets the rules and the refusal, so the graph is recorded again.
        graph.clear();
        record_increment(graph, field, K_CELLS);
        const Result<void> rebuilt = graph.compile();
        if (graph.refusal() != nullptr || !rebuilt)
        {
            std::fprintf(stderr, "errors_demo: the corrected rule did not compile\n");
            return 1;
        }

        const auto report = graph.run(K_STEPS);
        if (!report)
        {
            print_error("run()", report.error());
            return 1;
        }

        for (std::size_t i = 0; i < K_CELLS; ++i)
            is_correct = is_correct && field[i] == K_STEPS;
        is_correct = is_correct && graph.compile_count() == 1;

        std::printf("  after clear(): %zu steps, cell = %u, compiles = %zu\n", K_STEPS, field[0],
                    graph.compile_count());
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_errors_demo();
}
