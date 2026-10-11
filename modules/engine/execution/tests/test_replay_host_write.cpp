/**************************************************************************/
/* test_replay_host_write.cpp                                             */
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
 * @file test_replay_host_write.cpp
 * @brief Tests that a replayed plan reads what the host wrote between two executions.
 * @see modules/engine/execution/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace
{
    using SushiRuntime::Execution::IExecutionEngine;
    using SushiRuntime::Execution::RuntimeContext;
    using SushiRuntime::Graph::CompiledGraph;
    using SushiRuntime::Graph::TaskGraph;
    using SushiRuntime::Memory::ResourceRegion;
    using SushiRuntime::Memory::USMKind;

    /** @brief Holds how many times each plan is replayed. */
    constexpr std::uint32_t K_REPLAYS = 8;

    /** @brief Owns one word of USM on one device of a context. */
    class Word final
    {
        public:
            /** @brief Allocates the word through @p context, which outlives this object. */
            Word(RuntimeContext& context, USMKind kind, std::size_t device)
                : context_(context),
                  data_(static_cast<std::uint32_t*>(context.memory().allocate(
                      {kind, sizeof(std::uint32_t), 0, device})))
            {

            }

            /** @brief Releases the word. */
            ~Word()
            {
                if (data_ != nullptr)
                    context_.memory().release(data_);
            }

            Word(const Word&) = delete;
            Word& operator=(const Word&) = delete;
            Word(Word&&) = delete;
            Word& operator=(Word&&) = delete;

            /** @brief Returns the word, or nullptr when the allocation failed. */
            [[nodiscard]] std::uint32_t* data() const noexcept
            {
                return data_;
            }

        private:
            RuntimeContext& context_; /**< The context that allocated the word. */
            std::uint32_t* data_;     /**< The word. */
    };

    /** @brief Compiles a plan of one kernel that writes @p output as @p input plus one. */
    CompiledGraph compile_increment(std::uint32_t* input, std::uint32_t* output)
    {
        const std::vector<ResourceRegion> reads{ResourceRegion(input)};
        const std::vector<ResourceRegion> writes{ResourceRegion(output)};

        TaskGraph graph;
        graph.add_node([input, output](sycl::handler& handler)
        {
            handler.single_task([=]
            {
                *output = *input + 1u;
            });
        }, reads, writes);

        CompiledGraph plan = graph.compile();
        plan.capture_base_state();
        return plan;
    }

    /** @brief Replays one increment plan on @p device around a host write of its input. */
    void replay_around_host_writes(RuntimeContext& context, USMKind input_kind, std::size_t device)
    {
        const Word input(context, input_kind, device);
        const Word output(context, USMKind::Shared, device);
        ASSERT_NE(input.data(), nullptr);
        ASSERT_NE(output.data(), nullptr);

        IExecutionEngine& engine = context.get_execution_engine();
        CompiledGraph plan = compile_increment(input.data(), output.data());

        for (std::uint32_t value = 0; value < K_REPLAYS; ++value)
        {
            *input.data() = value;
            if (value != 0)
                plan.rearm();
            engine.execute(plan);
            EXPECT_EQ(*output.data(), value + 1u)
                << "device " << device << ", replay " << value;
        }
    }
} // namespace

TEST(Integration_ReplayHostWrite, AReplayReadsASharedWordTheHostWrote)
{
    RuntimeContext context;
    for (std::size_t device = 0; device < context.devices().count(); ++device)
        replay_around_host_writes(context, USMKind::Shared, device);
}

TEST(Integration_ReplayHostWrite, AReplayReadsAHostWordTheHostWrote)
{
    RuntimeContext context;
    for (std::size_t device = 0; device < context.devices().count(); ++device)
        replay_around_host_writes(context, USMKind::Host, device);
}
