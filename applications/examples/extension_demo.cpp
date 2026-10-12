/**************************************************************************/
/* extension_demo.cpp                                                     */
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
 * @file extension_demo.cpp
 * @brief Records an operation of a small domain library through extend() and the Recorder.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Holds a domain library that records its operations on the Recorder it is handed. */
namespace ScaleLibrary
{
    /**
     * @brief Records out = in * factor + offset as one rule of two nodes.
     * @return The position of the rule, as Graph::rule_registration() takes it.
     * @pre in and out hold the same number of elements, on one device.
     */
    inline std::size_t record_scale_offset(Recorder& recorder, const Buffer<std::uint32_t>& in,
                                           Buffer<std::uint32_t>& out, std::uint32_t factor,
                                           std::uint32_t offset)
    {
        // The scratch belongs to the recorder, which frees it with the nodes that use it.
        const std::size_t count = in.size();
        std::uint32_t* stage =
            recorder.scratch<std::uint32_t>(count, in.device_index(), in.residency()).value();
        const BufferRegion staged{stage, 0, count * sizeof(std::uint32_t)};
        const std::uint32_t* source = in.data();
        std::uint32_t* target = out.data();

        NodeOptions add_offset;
        add_offset.name = "add_offset";

        // Every node recorded while the scope lives belongs to one rule.
        auto rule = recorder.rule();
        recorder.kernel(count, Access(Reads(in), Writes(staged)),
                        [source, stage, factor](std::size_t i) { stage[i] = source[i] * factor; });
        return recorder.task(
            Access(Reads(staged), Writes(out)),
            [stage, target, count, offset](sycl::queue& queue,
                                           const std::vector<sycl::event>& after)
            {
                return queue.submit([&](sycl::handler& handler)
                {
                    handler.depends_on(after);
                    handler.parallel_for(sycl::range<1>(count),
                                         [stage, target, offset](sycl::id<1> i)
                                         {
                                             target[i] = stage[i] + offset;
                                         });
                });
            },
            add_offset);
    }
} // namespace ScaleLibrary

namespace
{
    /** @brief Elements of the two buffers. */
    constexpr std::size_t K_COUNT = 1024;

    /** @brief Factor the library's operation multiplies by. */
    constexpr std::uint32_t K_FACTOR = 3;

    /** @brief Offset the library's operation adds. */
    constexpr std::uint32_t K_OFFSET = 7;

    /** @brief Records a fill and the library's operation, prints the plan and checks a run. */
    int run_extension_demo()
    {
        auto runtime = Runtime::create().value();
        auto input = runtime.buffer<std::uint32_t>(K_COUNT).value();
        auto output = runtime.buffer<std::uint32_t>(K_COUNT).value();

        // extend() hands the graph's Recorder to the library; name() names the rule it recorded.
        std::size_t scale_rule = 0;
        auto graph = runtime.graph();
        graph.add(K_COUNT, Out(input),
                  [](std::size_t i, std::uint32_t* in) { in[i] = static_cast<std::uint32_t>(i); })
            .name("fill")
            .extend([&](Recorder& recorder)
            {
                scale_rule = ScaleLibrary::record_scale_offset(recorder, input, output, K_FACTOR,
                                                               K_OFFSET);
            })
            .name("scale_offset");

        const PlanView plan = graph.plan().value();
        std::printf("extension: %zu rules, %zu nodes, the library recorded rule %zu\n",
                    graph.size(), graph.node_count(), scale_rule);
        for (const PlanNode& node : plan.nodes)
            std::printf("  rule %zu  %-14s waits on %zu\n", node.rule, node.name.c_str(),
                        node.predecessor_count);

        graph.run().value();

        // The rule has a registration index once the graph compiled; node events report under it.
        const bool is_registered = graph.rule_registration(scale_rule) != Graph::K_NO_REGISTRATION;
        bool is_correct = graph.size() == 2 && graph.node_count() == 3 && is_registered &&
                          scale_rule == graph.last_rule() && plan.nodes.size() == 3 &&
                          plan.nodes[1].rule == scale_rule && plan.nodes[2].rule == scale_rule;
        for (std::size_t i = 0; i < K_COUNT; ++i)
            is_correct = is_correct && output[i] == i * K_FACTOR + K_OFFSET;

        std::printf("  output[%zu] = %u  (expected %zu)\n", K_COUNT - 1, output[K_COUNT - 1],
                    (K_COUNT - 1) * K_FACTOR + K_OFFSET);
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_extension_demo();
}
