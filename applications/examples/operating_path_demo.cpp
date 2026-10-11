/**************************************************************************/
/* operating_path_demo.cpp                                                */
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
 * @file operating_path_demo.cpp
 * @brief Runs a configured runtime through try_run, a late-bound launch and a device readback.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstddef>
#include <cstdio>
#include <vector>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Elements the buffer is allocated for; the live count never exceeds it. */
    constexpr std::size_t K_CAPACITY = 4096;

    /** @brief Steps of each of the two runs. */
    constexpr std::size_t K_STEPS = 8;

    /** @brief Returns what a caller does about a failed run, chosen by its code. */
    const char* remedy_for(ErrorCode code) noexcept
    {
        switch (code)
        {
            case ErrorCode::InvalidGraph:
                return "record a rule before running";
            case ErrorCode::DanglingResource:
                return "keep every buffer alive as long as the graph that names it";
            case ErrorCode::KernelFailed:
            case ErrorCode::DeviceLost:
                return "the device failed; stop, or build a new runtime";
            default:
                return "read the message";
        }
    }

    /** @brief Prints the code, the message and the remedy of a failed run. */
    void print_failure(const char* run_name, const Error& error)
    {
        std::fprintf(stderr, "operating_path_demo: %s failed with %s: %s (%s)\n", run_name,
                     to_string(error.code), error.message.c_str(), remedy_for(error.code));
    }

    /**
     * @brief Runs the demo on a runtime built from one RuntimeConfig.
     * @return 0 when both runs succeed and the readback holds the expected counts.
     * @throws SushiException When the runtime cannot be built or a range call is refused.
     */
    int run_demo()
    {
        // Two floating workers leave the other cores to the program that hosts the runtime.
        RuntimeConfig config;
        config.worker_count = 2;
        config.pinning = PinPolicy::Float;
        auto runtime = Runtime::create(config);

        // advanced() answers what was built: the devices by index and the worker pool.
        const AdvancedControls built = runtime.advanced();
        for (const auto& device : built.devices())
            std::printf("  device %zu: %s\n", device.index, device.name.c_str());
        std::printf("  workers: %zu\n", built.worker_pool().count);

        // A device-resident buffer is not host-addressable, so write_range() seeds it.
        auto hits = runtime.buffer<float>(K_CAPACITY, DeviceIndex{0}, Residency::Device);
        const std::vector<float> zeros(K_CAPACITY, 0.0f);
        hits.write_range({0, K_CAPACITY}, zeros);

        // sized() is polled before each step, so the launch width changes with no recompile.
        std::size_t live_count = K_CAPACITY / 4;
        float* cells = hits.data();
        auto graph = runtime.graph();
        graph.add(Dynamic().sized([&live_count] { return live_count; }), K_CAPACITY, Reads(),
                  Writes(hits), [cells](std::size_t index) { cells[index] += 1.0f; })
            .name("count_hits");

        // try_run() returns the report or an Error and throws nothing.
        const auto first = graph.try_run(K_STEPS);
        if (!first)
        {
            print_failure("the first run", first.error());
            return 1;
        }

        // The second run covers twice as many elements on the plan the first one compiled.
        live_count = K_CAPACITY / 2;
        const auto second = graph.try_run(K_STEPS);
        if (!second)
        {
            print_failure("the second run", second.error());
            return 1;
        }

        // A graph with no rule is refused, and the caller tells the refusal apart by its ErrorCode.
        auto empty = runtime.graph();
        const auto refused = empty.try_run();
        if (refused || refused.error().code != ErrorCode::InvalidGraph)
        {
            std::fprintf(stderr, "operating_path_demo: an empty graph was not refused\n");
            return 1;
        }
        std::printf("  empty graph: %s, so %s\n", to_string(refused.error().code),
                    remedy_for(refused.error().code));

        // read_range() copies one window through the owning queue and maps nothing else.
        const float in_both_runs = hits.read_range({0, 1})[0];
        const float in_second_run = hits.read_range({K_CAPACITY / 4, 1})[0];
        const float in_neither_run = hits.read_range({K_CAPACITY / 2, 1})[0];

        const bool is_correct = in_both_runs == static_cast<float>(2 * K_STEPS) &&
                                in_second_run == static_cast<float>(K_STEPS) &&
                                in_neither_run == 0.0f;

        std::printf("operating_path_demo: 2 runs of %zu steps, compiles = %zu\n", K_STEPS,
                    graph.compile_count());
        std::printf("  hits = %.0f, %.0f, %.0f  (expected %zu, %zu, 0)\n", in_both_runs,
                    in_second_run, in_neither_run, 2 * K_STEPS, K_STEPS);
        std::printf("  read back %zu bytes; result: %s\n", built.device_to_host_bytes(),
                    is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return run_demo();
    }
    catch (const SushiException& error)
    {
        print_failure("setup", error.error());
        return 1;
    }
}
