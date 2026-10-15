/**************************************************************************/
/* test_builder_surface.cpp                                               */
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
 * @file test_builder_surface.cpp
 * @brief Tests the add() overloads of BuilderSurface: launch first, then access, then callable.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

static_assert(std::is_trivially_copyable<Extent<2>>::value,
              "an Extent is copied into the replay callable of its node");
static_assert(std::is_trivially_copyable<NdRange<2>>::value,
              "an NdRange is copied into the replay callable of its node");

namespace
{
    /** @brief Names the element type of every handle here; unsigned, so overflow wraps. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 64;
    constexpr std::size_t K_PREFIX = 24;
    constexpr std::size_t K_ORIGIN = 16;
    constexpr std::size_t K_TILE = 8;
    constexpr Word K_SEED = 5;

    /** @brief Allocates count words on a device, each holding its index plus K_SEED. */
    Buffer<Word> seeded(Runtime& rt, std::size_t count = K_WIDTH,
                             DeviceIndex device = DeviceIndex{0})
    {
        auto buffer = rt.buffer<Word>(count, device).value();
        for (std::size_t i = 0; i < count; ++i)
            buffer[i] = static_cast<Word>(i) + K_SEED;
        return buffer;
    }

    /** @brief Allocates a State of count words whose current field holds index plus K_SEED. */
    State<Word> seeded_state(Runtime& rt, std::size_t count = K_WIDTH)
    {
        auto state = rt.state<Word>(count).value();
        for (std::size_t i = 0; i < count; ++i)
            state[i] = static_cast<Word>(i) + K_SEED;
        return state;
    }

    /**
     * @brief Returns the message of the refusal a call leaves on a recorder, or "" when none.
     * @param recorder Cleared before the call and after the refusal is read.
     */
    template <class Call>
    std::string refusal_of(Recorder& recorder, ErrorCode expected, Call&& call)
    {
        recorder.clear();
        call();
        const Error* refused = recorder.refusal();
        if (refused == nullptr)
            return std::string();
        EXPECT_EQ(refused->code, expected);
        const std::string message = refused->message;
        recorder.clear();
        return message;
    }

    /** @brief Tells whether a refusal message begins with the text given. */
    bool begins_with(const std::string& message, const std::string& text)
    {
        return message.compare(0, text.size(), text) == 0;
    }
} // namespace

/** @brief Tests that a count launch hands its kernel the pointer of each capture. */
TEST(Medium_BuilderSurface, CountLaunchWithCapturesPassesPointers)
{
    auto rt = Runtime::create().value();
    auto source = seeded(rt);
    auto target = seeded(rt);

    auto graph = rt.graph();
    graph.add(K_WIDTH, In(source), Out(target),
              [](std::size_t i, const Word* in, Word* out) { out[i] = in[i] * 2u; });
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(target[i], (static_cast<Word>(i) + K_SEED) * 2u) << "at i=" << i;
}

/** @brief Tests that an NdRange with an origin hands the shifted id to a two-index kernel. */
TEST(Medium_BuilderSurface, NdRangeOriginShiftsTheIDTheKernelReceives)
{
    auto rt = Runtime::create().value();
    auto target = seeded(rt);

    const NdRange<1> tiles =
        NdRange<1>(sycl::range<1>(K_PREFIX), sycl::range<1>(K_TILE)).at(sycl::id<1>(K_ORIGIN));
    EXPECT_EQ(tiles.origin[0], K_ORIGIN);
    EXPECT_EQ(tiles.count(), K_PREFIX);

    auto graph = rt.graph();
    graph.add(tiles, InOut(target), [](sycl::nd_item<1>, sycl::id<1> index, Word* words)
    {
        words[index[0]] = 0u;
    });
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
    {
        const bool inside = i >= K_ORIGIN && i < K_ORIGIN + K_PREFIX;
        ASSERT_EQ(target[i], inside ? 0u : static_cast<Word>(i) + K_SEED) << "at i=" << i;
    }
}

/** @brief Tests that add(count, state, kernel) updates the first count elements and no more. */
TEST(Medium_BuilderSurface, StateRuleOverACountUpdatesAPrefix)
{
    auto rt = Runtime::create().value();
    auto field = seeded_state(rt);

    auto graph = rt.graph();
    graph.add_rule(K_PREFIX, field, [](std::size_t i, const Word* current, Word* next)
    {
        next[i] = current[i] + 100u;
    });
    graph.run().value();

    for (std::size_t i = 0; i < K_PREFIX; ++i)
        ASSERT_EQ(field.current()[i], static_cast<Word>(i) + K_SEED + 100u) << "at i=" << i;
}

/** @brief Tests that a launch reaching past its State is refused where add() is called. */
TEST(Medium_BuilderSurface, ALaunchPastItsStateIsRefusedAtAdd)
{
    auto rt = Runtime::create().value();
    auto field = seeded_state(rt);
    auto graph = rt.graph();

    const Extent<1> window{sycl::range<1>(K_WIDTH), sycl::id<1>(K_ORIGIN)};
    const std::string refusal = refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
    {
        graph.add_rule(window, field, [](sycl::id<1>, const Word*, Word*) {});
    });

    EXPECT_NE(refusal.find("rule 0"), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("{" + std::to_string(K_WIDTH) + "} at origin {" +
                           std::to_string(K_ORIGIN) + "}"), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("holds " + std::to_string(K_WIDTH)), std::string::npos) << refusal;
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}

/** @brief Tests both coupled forms: the whole field and a launch, each reading two States. */
TEST(Medium_BuilderSurface, CoupledRulesReadEveryStateNamed)
{
    auto rt = Runtime::create().value();
    auto whole = seeded_state(rt);
    auto prefix = seeded_state(rt);
    auto input = seeded_state(rt);

    auto graph = rt.graph();
    graph.add_coupled(Coupled(whole, input),
                      [](std::size_t i, const Word* current, Word* next, const Word* other)
                      {
                  next[i] = current[i] + other[i];
                      });
    graph.add_coupled(K_PREFIX, Coupled(prefix, input),
                      [](std::size_t i, const Word* current, Word* next, const Word* other)
                      {
                  next[i] = current[i] * other[i];
                      });
    EXPECT_EQ(graph.size(), 2u);
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(whole.current()[i], (static_cast<Word>(i) + K_SEED) * 2u) << "at i=" << i;
    for (std::size_t i = 0; i < K_PREFIX; ++i)
    {
        const Word seed = static_cast<Word>(i) + K_SEED;
        ASSERT_EQ(prefix.current()[i], seed * seed) << "at i=" << i;
    }
}

/** @brief Tests that a coupled rule whose input is shorter than its output is refused. */
TEST(Medium_BuilderSurface, ACoupledInputSmallerThanTheOutputIsRefusedAtAdd)
{
    auto rt = Runtime::create().value();
    auto output = seeded_state(rt);
    auto input = seeded_state(rt, K_PREFIX);
    auto graph = rt.graph();

    const std::string refusal = refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
    {
        graph.add_coupled(Coupled(output, input),
                          [](std::size_t, const Word*, Word*, const Word*) {});
    });

    EXPECT_NE(refusal.find("the count " + std::to_string(K_WIDTH)), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("holds " + std::to_string(K_PREFIX)), std::string::npos) << refusal;
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}

/** @brief Tests the late-bound order: Dynamic, capacity, access, callable. */
TEST(Medium_BuilderSurface, DynamicTakesItsCapacityBeforeItsAccess)
{
    auto rt = Runtime::create().value();
    auto target = seeded(rt);
    Word* words = target.data();
    std::size_t live = K_PREFIX;

    auto graph = rt.graph();
    graph.add_dynamic(sized([&live] { return live; }), K_WIDTH, Reads(target),
                      Writes(target), [words](std::size_t i) { words[i] = 0u; });
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(target[i], i < K_PREFIX ? 0u : static_cast<Word>(i) + K_SEED) << "at i=" << i;
}

/** @brief Tests that untracked kernels placed with On() write buffers of the device named. */
TEST(Medium_BuilderSurface, UntrackedKernelRunsOnTheDeviceOfTheBufferNamed)
{
    auto rt = Runtime::create().value();
    const DeviceIndex last{rt.advanced().device_count() - 1};
    auto by_handle = seeded(rt, K_WIDTH, last);
    auto by_index = seeded(rt, K_WIDTH, last);
    Word* handle_words = by_handle.data();
    Word* index_words = by_index.data();

    // The two kernels are unordered; each is the only writer of its own buffer.
    auto graph = rt.graph();
    graph.add_untracked(K_WIDTH, On(by_handle),
                        [handle_words](std::size_t i) { handle_words[i] += 1u; });
    graph.add_untracked(On(last), [index_words](sycl::handler& handler)
    {
        handler.parallel_for(sycl::range<1>(K_WIDTH),
                             [index_words](sycl::id<1> i) { index_words[i[0]] += 2u; });
    });
    EXPECT_EQ(graph.size(), 2u);
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
    {
        ASSERT_EQ(by_handle[i], static_cast<Word>(i) + K_SEED + 1u) << "at i=" << i;
        ASSERT_EQ(by_index[i], static_cast<Word>(i) + K_SEED + 2u) << "at i=" << i;
    }
}

/** @brief Tests that both add_untracked() forms stop on an On() the runtime has no device for. */
TEST(Medium_BuilderSurfaceDeathTest, PlacementOnADeviceTheRuntimeLacksStops)
{
    auto rt = Runtime::create().value();
    const std::size_t count = rt.advanced().device_count();
    auto graph = rt.graph();
    const std::string detail =
        ": device index " + std::to_string(count) + " names no device; the runtime drives " +
        std::to_string(count) + " devices";

    EXPECT_DEATH(graph.add_untracked(K_WIDTH, On(DeviceIndex{count}), [](std::size_t) {}),
                 "add_untracked\\(launch, On\\(\\.\\.\\.\\), kernel\\)" + detail);
    EXPECT_DEATH(graph.add_untracked(On(DeviceIndex{count}), [](sycl::handler&) {}),
                 "add_untracked\\(On\\(\\.\\.\\.\\), kernel\\)" + detail);
}

/** @brief Tests that a graph watches the handle an On() names. */
TEST(Medium_BuilderSurface, PlacementWatchesTheHandleItNames)
{
    auto rt = Runtime::create().value();
    auto graph = rt.graph();

    {
        auto freed = seeded(rt);
        Word* words = freed.data();
        graph.add_untracked(K_WIDTH, On(freed), [words](std::size_t i) { words[i] = 0u; });
    }
    const auto result = graph.run();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::DanglingResource);
}

/** @brief Tests that extend() runs its callable once and counts what it records as one rule. */
TEST(Medium_BuilderSurface, ExtendRecordsOneNamedRule)
{
    auto rt = Runtime::create().value();
    auto target = seeded(rt);
    Word* words = target.data();
    std::size_t calls = 0;

    auto graph = rt.graph();
    graph.extend([&](Recorder& recorder)
    {
        ++calls;
        for (std::size_t node = 0; node < 2; ++node)
            recorder.kernel(K_WIDTH, Access(Reads(target), Writes(target)),
                            [words](std::size_t i) { words[i] += 1u; });
    }).name("twice");

    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(graph.size(), 1u);
    EXPECT_EQ(graph.node_count(), 2u);
    graph.run().value();
    EXPECT_EQ(calls, 1u) << "an extension is not replayed when the plan is built";

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(target[i], static_cast<Word>(i) + K_SEED + 2u) << "at i=" << i;
}

/** @brief Tests that each refusal names the add() call the user wrote, through the public add(). */
TEST(Medium_BuilderSurface, RefusalsNameTheCallTheUserWrote)
{
    auto rt = Runtime::create().value();
    auto field = seeded_state(rt);
    auto output = seeded_state(rt);
    auto input = seeded_state(rt, K_PREFIX);
    State<Word> unallocated;
    auto graph = rt.graph();
    const Extent<1> window{sycl::range<1>(K_WIDTH), sycl::id<1>(K_ORIGIN)};

    const std::string state_refusal = refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
    {
        graph.add_rule(window, field, [](sycl::id<1>, const Word*, Word*) {});
    });
    EXPECT_TRUE(begins_with(state_refusal, "add_rule(launch, state, kernel), rule 0: "))
        << state_refusal;

    const std::string empty_refusal = refusal_of(graph.recorder(), ErrorCode::InvalidAccess, [&]
    {
        graph.add_rule(unallocated, [](std::size_t, const Word*, Word*) {});
    });
    EXPECT_TRUE(begins_with(empty_refusal, "add_rule(state, kernel), rule 0: ")) << empty_refusal;

    const std::string coupled_refusal = refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
    {
        graph.add_coupled(Coupled(output, input),
                          [](std::size_t, const Word*, Word*, const Word*) {});
    });
    EXPECT_TRUE(begins_with(coupled_refusal, "add_coupled(Coupled(out, in0, ...), kernel), rule 0: "))
        << coupled_refusal;

    const std::string launch_coupled_refusal =
        refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
    {
        graph.add_coupled(window, Coupled(output, input),
                          [](sycl::id<1>, const Word*, Word*, const Word*) {});
    });
    EXPECT_TRUE(begins_with(launch_coupled_refusal,
                            "add_coupled(launch, Coupled(out, in0, ...), kernel), rule 0: "))
        << launch_coupled_refusal;

    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}

/** @brief Tests that a refused add is kept, records nothing, and is what the next run returns. */
TEST(Medium_BuilderSurface, ARefusedAddIsKeptAndReturnedByTheNextRun)
{
    auto rt = Runtime::create().value();
    auto field = seeded_state(rt);
    auto target = seeded(rt);
    Word* words = target.data();
    const Word before = words[0];
    State<Word> unallocated;
    auto graph = rt.graph();
    const Extent<1> window{sycl::range<1>(K_WIDTH), sycl::id<1>(K_ORIGIN)};

    EXPECT_EQ(graph.refusal(), nullptr);
    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });
    graph.add_rule(window, field, [](sycl::id<1>, const Word*, Word*) {});
    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });

    const Error* refused = graph.refusal();
    ASSERT_NE(refused, nullptr);
    EXPECT_EQ(refused->code, ErrorCode::InvalidGraph);
    EXPECT_TRUE(begins_with(refused->message, "add_rule(launch, state, kernel), rule 1: "))
        << refused->message;
    EXPECT_EQ(graph.size(), 2u) << "the valid rules are recorded, the refused one is not";

    // A later refusal does not replace the first.
    graph.add_rule(unallocated, [](std::size_t, const Word*, Word*) {});
    ASSERT_NE(graph.refusal(), nullptr);
    EXPECT_EQ(graph.refusal()->code, ErrorCode::InvalidGraph);

    const auto first_run = graph.run();
    ASSERT_FALSE(first_run.has_value());
    EXPECT_EQ(first_run.error().code, ErrorCode::InvalidGraph);
    EXPECT_EQ(first_run.error().message, graph.refusal()->message);
    EXPECT_EQ(words[0], before) << "a run that returns a refusal runs nothing";

    const auto second_run = graph.run();
    ASSERT_FALSE(second_run.has_value()) << "the refusal stays until clear()";

    graph.recorder().clear();
    EXPECT_EQ(graph.refusal(), nullptr);
    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });
    const auto valid_run = graph.run();
    ASSERT_TRUE(valid_run.has_value());
    EXPECT_EQ(words[0], before + 1u);
}
