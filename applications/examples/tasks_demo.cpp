/**************************************************************************/
/* tasks_demo.cpp                                                         */
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
 * @file tasks_demo.cpp
 * @brief Runs a task body and a host task between kernels and prints the plan they compile to.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Elements of every buffer but the checksum. */
    constexpr std::size_t K_COUNT = 1024;

    /** @brief Nodes the plan must wait for, in recording order. */
    constexpr std::size_t K_EXPECTED_PREDECESSORS[] = {0, 1, 1, 1, 0};

    /** @brief Returns options that state a node's name and nothing else. */
    NodeOptions named(std::string_view name)
    {
        NodeOptions options;
        options.name = name;
        return options;
    }

    /** @brief Records five nodes, prints their plan and checks what a run of them wrote. */
    int run_tasks_demo()
    {
        auto runtime = Runtime::create().value();
        auto values = runtime.buffer<std::uint32_t>(K_COUNT).value();
        auto staged = runtime.buffer<std::uint32_t>(K_COUNT).value();
        auto result = runtime.buffer<std::uint32_t>(K_COUNT).value();
        auto apart = runtime.buffer<std::uint32_t>(K_COUNT).value();
        auto checksum = runtime.buffer<std::uint64_t>(1).value();

        const std::uint32_t* v = values.data();
        std::uint32_t* s = staged.data();
        std::uint32_t* r = result.data();
        std::uint32_t* a = apart.data();
        std::uint64_t* sum = checksum.data();

        auto graph = runtime.graph();
        graph.add(K_COUNT, Out(values),
                  [](std::size_t i, std::uint32_t* out) { out[i] = static_cast<std::uint32_t>(i); },
                  named("fill"));

        // The body submits two command groups and returns the event of the last one.
        graph.add_task(Reads(values), Writes(staged, result),
                       [v, s, r](sycl::queue& queue, const std::vector<sycl::event>& after)
                       {
                           const sycl::event doubled = queue.submit([&](sycl::handler& handler)
                           {
                               handler.depends_on(after);
                               handler.parallel_for(sycl::range<1>(K_COUNT),
                                                    [v, s](sycl::id<1> i) { s[i] = v[i] * 2u; });
                           });
                           return queue.submit([&](sycl::handler& handler)
                           {
                               handler.depends_on(doubled);
                               handler.parallel_for(sycl::range<1>(K_COUNT),
                                                    [s, r](sycl::id<1> i) { r[i] = s[i] + 1u; });
                           });
                       },
                       named("double_then_add_one"));

        graph.add(K_COUNT, InOut(result),
                  [](std::size_t i, std::uint32_t* out) { out[i] += 3u; }, named("add_three"));

        // A host task runs on a worker thread and reads shared memory its predecessors wrote.
        graph.add_host(Reads(result), Writes(checksum),
                       [r, sum]()
                       {
                           std::uint64_t total = 0;
                           for (std::size_t i = 0; i < K_COUNT; ++i)
                               total += r[i];
                           sum[0] = total;
                       },
                       named("checksum"));

        // An untracked kernel names no data, so the plan orders it against nothing.
        graph.add_untracked(K_COUNT, [a](std::size_t i) { a[i] = 7u; }, named("apart"));

        const Result<PlanView> plan = graph.plan();
        if (!plan)
        {
            std::fprintf(stderr, "tasks_demo: plan() failed with %s: %s\n",
                         to_string(plan.error().code), plan.error().message.c_str());
            return 1;
        }

        std::printf("tasks: %zu rules, %zu nodes\n", graph.size(), graph.node_count());
        bool is_correct = plan->nodes.size() == 5;
        for (std::size_t node = 0; node < plan->nodes.size(); ++node)
        {
            const PlanNode& described = plan->nodes[node];
            std::printf("  rule %zu  %-20s waits on %zu\n", described.rule,
                        described.name.c_str(), described.predecessor_count);
            is_correct = is_correct &&
                         described.predecessor_count == K_EXPECTED_PREDECESSORS[node];
        }

        graph.run().value();

        // result[i] is 2 * i + 4, and the checksum is the sum of it over every element.
        const std::uint64_t expected_sum = K_COUNT * (K_COUNT - 1) + 4 * K_COUNT;
        for (std::size_t i = 0; i < K_COUNT; ++i)
            is_correct = is_correct && result[i] == 2 * i + 4 && apart[i] == 7u;
        is_correct = is_correct && checksum[0] == expected_sum && graph.compile_count() == 1;

        std::printf("  checksum = %llu  (expected %llu)\n",
                    static_cast<unsigned long long>(checksum[0]),
                    static_cast<unsigned long long>(expected_sum));
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_tasks_demo();
}
