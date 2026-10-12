/**************************************************************************/
/* data_access_demo.cpp                                                   */
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
 * @file data_access_demo.cpp
 * @brief Reads and writes buffers of each residency, through range calls and the device queue.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Elements of the two large buffers. */
    constexpr std::size_t K_COUNT = 4096;

    /** @brief Byte alignment asked for the device-resident buffer. */
    constexpr std::size_t K_ALIGNMENT = 64;

    /** @brief Elements of the window read back from the end of the device-resident buffer. */
    constexpr std::size_t K_WINDOW = 4;

    /** @brief Prints a failed call with its error and returns the exit code of a failure. */
    int report_failure(const char* call, const Error& error)
    {
        std::fprintf(stderr, "data_access_demo: %s failed with %s: %s\n", call,
                     to_string(error.code), error.message.c_str());
        return 1;
    }

    /** @brief Prints where a buffer lives and whether the host may index it. */
    void print_buffer(const char* name, const Buffer<float>& buffer)
    {
        std::printf("  %-8s %-6s host-addressable: %s\n", name, residency_name(buffer.residency()),
                    buffer.is_host_accessible() ? "yes" : "no");
    }

    /** @brief Returns the values 0, 1, 2 and so on, one per element. */
    std::vector<float> ramp(std::size_t count)
    {
        std::vector<float> values(count);
        for (std::size_t i = 0; i < count; ++i)
            values[i] = static_cast<float>(i);
        return values;
    }

    /** @brief Moves data through each residency and checks every copy against the host. */
    int run_data_access_demo()
    {
        auto runtime = Runtime::create().value();

        // The host indexes shared and host memory, and reaches device memory by range calls.
        auto doubled = runtime.buffer<float>(K_COUNT).value();
        auto resident =
            runtime.buffer<float>(K_COUNT, DeviceIndex{0}, Residency::Device, K_ALIGNMENT).value();
        auto first = runtime.buffer<float>(1, DeviceIndex{0}, Residency::Host).value();

        std::printf("data access: %zu floats\n", K_COUNT);
        print_buffer("doubled", doubled);
        print_buffer("resident", resident);
        print_buffer("first", first);

        const std::vector<float> seed = ramp(K_COUNT);
        const Result<void> seeded = resident.write_range({0, K_COUNT}, seed);
        if (!seeded)
            return report_failure("write_range()", seeded.error());

        // Work the caller submits on the queue is outside every graph, so the caller waits for it.
        float* cells = resident.data();
        sycl::queue& queue = runtime.advanced().queue(resident.device_index());
        queue.parallel_for(sycl::range<1>(K_COUNT), [cells](sycl::id<1> i) { cells[i] += 1.0f; });
        const Result<void> idle = runtime.wait();
        if (!idle)
            return report_failure("Runtime::wait()", idle.error());

        auto graph = runtime.graph();
        graph.add(K_COUNT, In(resident), Out(doubled),
                  [](std::size_t i, const float* in, float* out) { out[i] = in[i] * 2.0f; })
            .add(std::size_t{1}, In(resident), Out(first),
                 [](std::size_t, const float* in, float* out) { out[0] = in[0]; });
        graph.run().value();

        // read_range() copies one window and waits for it; the rest of the buffer stays put.
        const Result<std::vector<float>> tail = resident.read_range({K_COUNT - K_WINDOW, K_WINDOW});
        if (!tail)
            return report_failure("read_range()", tail.error());

        // A copy the caller submits itself goes through the same queue.
        std::vector<float> copied(K_COUNT, 0.0f);
        queue.memcpy(copied.data(), cells, K_COUNT * sizeof(float)).wait();

        const AdvancedControls counters = runtime.advanced();
        const bool is_aligned = reinterpret_cast<std::uintptr_t>(cells) % K_ALIGNMENT == 0;
        bool is_correct = is_aligned && tail->size() == K_WINDOW && first[0] == 1.0f &&
                          counters.host_to_device_bytes() == K_COUNT * sizeof(float) &&
                          counters.device_to_host_bytes() == K_WINDOW * sizeof(float);
        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            const float expected = static_cast<float>(i + 1);
            is_correct = is_correct && copied[i] == expected && doubled[i] == 2.0f * expected;
        }
        for (std::size_t i = 0; is_correct && i < K_WINDOW; ++i)
            is_correct = (*tail)[i] == static_cast<float>(K_COUNT - K_WINDOW + i + 1);

        std::printf("  resident is aligned to %zu bytes: %s\n", K_ALIGNMENT,
                    is_aligned ? "yes" : "no");
        std::printf("  first[0] = %.0f, doubled[%zu] = %.0f\n", first[0], K_COUNT - 1,
                    doubled[K_COUNT - 1]);
        std::printf("  range calls moved %zu bytes to the device and %zu back\n",
                    counters.host_to_device_bytes(), counters.device_to_host_bytes());
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_data_access_demo();
}
