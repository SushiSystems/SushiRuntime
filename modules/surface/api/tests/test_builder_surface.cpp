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
 * @author Mustafa Garip
 * @see modules/surface/api/tests/README.md
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

static_assert(std::is_trivially_copyable<API::Extent<2>>::value,
              "an Extent is copied into the replay callable of its node");
static_assert(std::is_trivially_copyable<API::NdRange<2>>::value,
              "an NdRange is copied into the replay callable of its node");

namespace
{
    /** @brief Names the element type of every handle here; unsigned, so overflow wraps. */
    using Word = std::uint32_t;

    constexpr std::size_t WIDTH = 64;
    constexpr std::size_t PREFIX = 24;
    constexpr std::size_t ORIGIN = 16;
    constexpr std::size_t TILE = 8;
    constexpr Word SEED = 5;

    /** @brief Allocates count words on a device, each holding its index plus SEED. */
    API::Buffer<Word> seeded(Runtime& rt, std::size_t count = WIDTH,
                             API::DeviceIndex device = API::DeviceIndex{0})
    {
        auto buffer = rt.buffer<Word>(count, device);
        for (std::size_t i = 0; i < count; ++i)
            buffer[i] = static_cast<Word>(i) + SEED;
        return buffer;
    }

    /** @brief Allocates a State of count words whose current field holds index plus SEED. */
    API::State<Word> seeded_state(Runtime& rt, std::size_t count = WIDTH)
    {
        auto state = rt.state<Word>(count);
        for (std::size_t i = 0; i < count; ++i)
            state[i] = static_cast<Word>(i) + SEED;
        return state;
    }

    /** @brief Returns the message of the error a builder call throws, or "" when none. */
    template <class Call>
    std::string refusal_of(Core::Errc expected, Call&& call)
    {
        try
        {
            call();
        }
        catch (const Core::SushiException& refused)
        {
            EXPECT_EQ(refused.error().code, expected);
            return refused.error().message;
        }
        return std::string();
    }

    /** @brief Tells whether a refusal message begins with the text given. */
    bool begins_with(const std::string& message, const std::string& text)
    {
        return message.compare(0, text.size(), text) == 0;
    }

    /** @brief Holds what one two-rule graph recorded and produced. */
    struct Outcome
    {
        std::size_t rules = 0;   /**< Rules the graph recorded. */
        std::size_t nodes = 0;   /**< Nodes the graph recorded. */
        std::vector<Word> words; /**< Each word of the target after the run. */
    };

    /**
     * @brief Records one rule over a seeded buffer, then a rule that triples it, and runs both.
     * @tparam RecordFirst Callable invoked as record_first(graph, target, words).
     */
    template <class RecordFirst>
    Outcome increment_then_triple(Runtime& rt, RecordFirst&& record_first)
    {
        auto target = seeded(rt);
        Word* words = target.data();
        auto graph = rt.graph();
        record_first(graph, target, words);
        graph.add(WIDTH, API::Reads(target), API::Writes(target),
                  [words](std::size_t i) { words[i] *= 3u; });

        Outcome outcome;
        outcome.rules = graph.size();
        outcome.nodes = graph.node_count();
        graph.run();
        for (std::size_t i = 0; i < WIDTH; ++i)
            outcome.words.push_back(target[i]);
        return outcome;
    }
} // namespace

/** @brief Tests that a count launch hands its kernel the pointer of each capture. */
TEST(Integration_BuilderSurface, CountLaunchWithCapturesPassesPointers)
{
    auto rt = Runtime::create();
    auto source = seeded(rt);
    auto target = seeded(rt);

    auto graph = rt.graph();
    graph.add(WIDTH, API::In(source), API::Out(target),
              [](std::size_t i, const Word* in, Word* out) { out[i] = in[i] * 2u; });
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(target[i], (static_cast<Word>(i) + SEED) * 2u) << "at i=" << i;
}

/** @brief Tests that an NdRange with an origin hands the shifted id to a two-index kernel. */
TEST(Integration_BuilderSurface, NdRangeOriginShiftsTheIDTheKernelReceives)
{
    auto rt = Runtime::create();
    auto target = seeded(rt);

    const API::NdRange<1> tiles =
        API::NdRange<1>(sycl::range<1>(PREFIX), sycl::range<1>(TILE)).at(sycl::id<1>(ORIGIN));
    EXPECT_EQ(tiles.origin[0], ORIGIN);
    EXPECT_EQ(tiles.count(), PREFIX);

    auto graph = rt.graph();
    graph.add(tiles, API::InOut(target), [](sycl::nd_item<1>, sycl::id<1> index, Word* words)
    {
        words[index[0]] = 0u;
    });
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
    {
        const bool inside = i >= ORIGIN && i < ORIGIN + PREFIX;
        ASSERT_EQ(target[i], inside ? 0u : static_cast<Word>(i) + SEED) << "at i=" << i;
    }
}

/** @brief Tests that add(count, state, fn) updates the first count elements and no more. */
TEST(Integration_BuilderSurface, StateRuleOverACountUpdatesAPrefix)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);

    auto graph = rt.graph();
    graph.add(PREFIX, field, [](std::size_t i, const Word* current, Word* next)
    {
        next[i] = current[i] + 100u;
    });
    graph.run();

    for (std::size_t i = 0; i < PREFIX; ++i)
        ASSERT_EQ(field.current()[i], static_cast<Word>(i) + SEED + 100u) << "at i=" << i;
}

/** @brief Tests that a launch reaching past its State is refused where add() is called. */
TEST(Integration_BuilderSurface, ALaunchPastItsStateIsRefusedAtAdd)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    auto graph = rt.graph();

    const API::Extent<1> window{sycl::range<1>(WIDTH), sycl::id<1>(ORIGIN)};
    const std::string refusal = refusal_of(Core::Errc::invalid_graph, [&]
    {
        graph.add(window, field, [](sycl::id<1>, const Word*, Word*) {});
    });

    EXPECT_NE(refusal.find("rule 0"), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("{" + std::to_string(WIDTH) + "} at origin {" +
                           std::to_string(ORIGIN) + "}"), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("holds " + std::to_string(WIDTH)), std::string::npos) << refusal;
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}

/** @brief Tests both coupled forms: the whole field and a launch, each reading two States. */
TEST(Integration_BuilderSurface, CoupledRulesReadEveryStateNamed)
{
    auto rt = Runtime::create();
    auto whole = seeded_state(rt);
    auto prefix = seeded_state(rt);
    auto input = seeded_state(rt);

    auto graph = rt.graph();
    graph.add(API::Coupled(whole, input),
              [](std::size_t i, const Word* current, Word* next, const Word* other)
              {
                  next[i] = current[i] + other[i];
              });
    graph.add(PREFIX, API::Coupled(prefix, input),
              [](std::size_t i, const Word* current, Word* next, const Word* other)
              {
                  next[i] = current[i] * other[i];
              });
    EXPECT_EQ(graph.size(), 2u);
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(whole.current()[i], (static_cast<Word>(i) + SEED) * 2u) << "at i=" << i;
    for (std::size_t i = 0; i < PREFIX; ++i)
    {
        const Word seed = static_cast<Word>(i) + SEED;
        ASSERT_EQ(prefix.current()[i], seed * seed) << "at i=" << i;
    }
}

/** @brief Tests that a coupled rule whose input is shorter than its output is refused. */
TEST(Integration_BuilderSurface, ACoupledInputSmallerThanTheOutputIsRefusedAtAdd)
{
    auto rt = Runtime::create();
    auto output = seeded_state(rt);
    auto input = seeded_state(rt, PREFIX);
    auto graph = rt.graph();

    const std::string refusal = refusal_of(Core::Errc::invalid_graph, [&]
    {
        graph.add(API::Coupled(output, input),
                  [](std::size_t, const Word*, Word*, const Word*) {});
    });

    EXPECT_NE(refusal.find("the count " + std::to_string(WIDTH)), std::string::npos) << refusal;
    EXPECT_NE(refusal.find("holds " + std::to_string(PREFIX)), std::string::npos) << refusal;
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}

/** @brief Tests the late-bound order: Dynamic, capacity, access, callable. */
TEST(Integration_BuilderSurface, DynamicTakesItsCapacityBeforeItsAccess)
{
    auto rt = Runtime::create();
    auto target = seeded(rt);
    Word* words = target.data();
    std::size_t live = PREFIX;

    auto graph = rt.graph();
    graph.add(API::sized([&live] { return live; }), WIDTH, API::Reads(target),
              API::Writes(target), [words](std::size_t i) { words[i] = 0u; });
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(target[i], i < PREFIX ? 0u : static_cast<Word>(i) + SEED) << "at i=" << i;
}

/** @brief Tests that untracked kernels placed with On() write buffers of the device named. */
TEST(Integration_BuilderSurface, UntrackedKernelRunsOnTheDeviceOfTheBufferNamed)
{
    auto rt = Runtime::create();
    const API::DeviceIndex last{rt.advanced().device_count() - 1};
    auto by_handle = seeded(rt, WIDTH, last);
    auto by_index = seeded(rt, WIDTH, last);
    Word* handle_words = by_handle.data();
    Word* index_words = by_index.data();

    // The two kernels are unordered; each is the only writer of its own buffer.
    auto graph = rt.graph();
    graph.add_untracked(WIDTH, API::On(by_handle),
                        [handle_words](std::size_t i) { handle_words[i] += 1u; });
    graph.add_untracked(API::On(last), [index_words](sycl::handler& handler)
    {
        handler.parallel_for(sycl::range<1>(WIDTH),
                             [index_words](sycl::id<1> i) { index_words[i[0]] += 2u; });
    });
    EXPECT_EQ(graph.size(), 2u);
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
    {
        ASSERT_EQ(by_handle[i], static_cast<Word>(i) + SEED + 1u) << "at i=" << i;
        ASSERT_EQ(by_index[i], static_cast<Word>(i) + SEED + 2u) << "at i=" << i;
    }
}

/** @brief Tests that On() rejects a device the runtime lacks and watches the handle it names. */
TEST(Integration_BuilderSurface, PlacementIsCheckedAndItsHandleWatched)
{
    auto rt = Runtime::create();
    const std::size_t count = rt.advanced().device_count();
    auto graph = rt.graph();

    const std::string refusal = refusal_of(Core::Errc::invalid_access, [&]
    {
        graph.add_untracked(WIDTH, API::On(API::DeviceIndex{count}), [](std::size_t) {});
    });
    EXPECT_NE(refusal.find("DeviceIndex{" + std::to_string(count) + "}"), std::string::npos)
        << refusal;
    EXPECT_NE(refusal.find(std::to_string(count) + " device(s)"), std::string::npos) << refusal;
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";

    {
        auto freed = seeded(rt);
        Word* words = freed.data();
        graph.add_untracked(WIDTH, API::On(freed), [words](std::size_t i) { words[i] = 0u; });
    }
    const auto result = graph.try_run();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Core::Errc::dangling_resource);
}

/** @brief Tests that extend() runs its callable once and counts what it records as one rule. */
TEST(Integration_BuilderSurface, ExtendRecordsOneNamedRule)
{
    auto rt = Runtime::create();
    auto target = seeded(rt);
    Word* words = target.data();
    std::size_t calls = 0;

    auto graph = rt.graph();
    graph.extend([&](API::Recorder& recorder)
    {
        ++calls;
        for (std::size_t node = 0; node < 2; ++node)
            recorder.kernel(WIDTH, API::Access(API::Reads(target), API::Writes(target)),
                            [words](std::size_t i) { words[i] += 1u; });
    }).name("twice");

    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(graph.size(), 1u);
    EXPECT_EQ(graph.node_count(), 2u);
    graph.run();
    EXPECT_EQ(calls, 1u) << "an extension is not replayed when the plan is built";

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(target[i], static_cast<Word>(i) + SEED + 2u) << "at i=" << i;
}

/** @brief Tests that each refusal names the add() call the user wrote, through the public add(). */
TEST(Integration_BuilderSurface, RefusalsNameTheCallTheUserWrote)
{
    auto rt = Runtime::create();
    auto field = seeded_state(rt);
    auto output = seeded_state(rt);
    auto input = seeded_state(rt, PREFIX);
    auto target = seeded(rt);
    const std::size_t count = rt.advanced().device_count();
    auto graph = rt.graph();
    const API::Extent<1> window{sycl::range<1>(WIDTH), sycl::id<1>(ORIGIN)};

    const std::string state_refusal = refusal_of(Core::Errc::invalid_graph, [&]
    {
        graph.add(window, field, [](sycl::id<1>, const Word*, Word*) {});
    });
    EXPECT_TRUE(begins_with(state_refusal, "add(launch, state, fn), rule 0: ")) << state_refusal;

    const std::string coupled_refusal = refusal_of(Core::Errc::invalid_graph, [&]
    {
        graph.add(API::Coupled(output, input),
                  [](std::size_t, const Word*, Word*, const Word*) {});
    });
    EXPECT_TRUE(begins_with(coupled_refusal, "add(Coupled(out, in0, ...), fn), rule 0: "))
        << coupled_refusal;

    const std::string launch_coupled_refusal = refusal_of(Core::Errc::invalid_graph, [&]
    {
        graph.add(window, API::Coupled(output, input),
                  [](sycl::id<1>, const Word*, Word*, const Word*) {});
    });
    EXPECT_TRUE(begins_with(launch_coupled_refusal,
                            "add(launch, Coupled(out, in0, ...), fn), rule 0: "))
        << launch_coupled_refusal;

    const std::string untracked_refusal = refusal_of(Core::Errc::invalid_access, [&]
    {
        graph.add_untracked(WIDTH, API::On(API::DeviceIndex{count}), [](std::size_t) {});
    });
    EXPECT_TRUE(begins_with(untracked_refusal, "add_untracked(launch, On(...), fn): "))
        << untracked_refusal;

    const std::string raw_refusal = refusal_of(Core::Errc::invalid_access, [&]
    {
        graph.add_untracked(API::On(API::DeviceIndex{count}), [](sycl::handler&) {});
    });
    EXPECT_TRUE(begins_with(raw_refusal, "add_untracked(On(...), fn): ")) << raw_refusal;

    const std::string offload_refusal = refusal_of(Core::Errc::invalid_graph, [&]
    {
        graph.add("builder.check"_op, WIDTH, API::Reads(target), API::Writes(target),
                  [](std::size_t) {}, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    });
    EXPECT_TRUE(begins_with(offload_refusal, "add(OpID, n, reads, writes, local, params): "))
        << offload_refusal;

    const std::string offload_rule_refusal = refusal_of(Core::Errc::invalid_graph, [&]
    {
        graph.add("builder.check"_op, field, [](std::size_t, const Word*, Word*) {},
                  {1, 2, 3, 4, 5, 6, 7, 8, 9});
    });
    EXPECT_TRUE(begins_with(offload_rule_refusal, "add(OpID, state, local, params): "))
        << offload_rule_refusal;

    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/** @brief Tests that the spellings v0.4.0 deprecates record the same rules as the new ones. */
TEST(Integration_BuilderSurface, DeprecatedSpellingsForwardToTheNewOnes)
{
    auto rt = Runtime::create();
    auto target = seeded(rt);
    auto field = seeded_state(rt);
    auto output = seeded_state(rt);
    Word* words = target.data();

    API::Extent<1> window{sycl::range<1>(PREFIX), sycl::id<1>(ORIGIN)};
    EXPECT_EQ(window.offset[0], ORIGIN) << "offset names the same value as origin";
    window.offset = sycl::id<1>(0);
    EXPECT_EQ(window.origin[0], 0u);

    auto graph = rt.graph();
    graph.add(API::Reads(target), API::Writes(target), WIDTH,
              [words](std::size_t i) { words[i] += 1u; }).name_last("old order");
    graph.add(field, window, [](sycl::id<1> index, const Word* current, Word* next)
    {
        next[index[0]] = current[index[0]] + 1u;
    });
    graph.add(output, field, [](std::size_t i, const Word* current, Word* next, const Word* other)
    {
        next[i] = current[i] + other[i];
    });
    graph.add(API::sized([] { return PREFIX; }), API::Reads(target), API::Writes(target), WIDTH,
              [words](std::size_t i) { words[i] += 1u; });
    EXPECT_EQ(graph.size(), 4u);
    EXPECT_EQ(graph.rule_registration(0), API::Graph::no_registration);
    graph.run();

    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(target[i], static_cast<Word>(i) + SEED + (i < PREFIX ? 2u : 1u))
            << "at i=" << i;
    for (std::size_t i = 0; i < PREFIX; ++i)
        ASSERT_EQ(field.current()[i], static_cast<Word>(i) + SEED + 1u) << "at i=" << i;
    for (std::size_t i = 0; i < WIDTH; ++i)
        ASSERT_EQ(output.current()[i], (static_cast<Word>(i) + SEED) * 2u) << "at i=" << i;
}

/** @brief Tests that each raw-pointer form v0.4.0 deprecates records what its region form does. */
TEST(Integration_BuilderSurface, DeprecatedRawPointerFormsForwardToTheRegionForms)
{
    auto rt = Runtime::create();

    const auto expect_same = [](const Outcome& legacy, const Outcome& region)
    {
        EXPECT_EQ(legacy.rules, region.rules);
        EXPECT_EQ(legacy.nodes, region.nodes);
        EXPECT_EQ(legacy.words, region.words);
        for (std::size_t i = 0; i < legacy.words.size(); ++i)
            EXPECT_EQ(legacy.words[i], (static_cast<Word>(i) + SEED + 1u) * 3u) << "at i=" << i;
    };

    // Each first rule adds one; the triple after it waits for it, so the result is (x + 1) * 3.
    const Outcome per_element = increment_then_triple(rt, [](auto& graph, auto&, Word* words)
    {
        graph.add(std::vector<void*>{ words }, std::vector<void*>{ words }, WIDTH,
                  [words](std::size_t i) { words[i] += 1u; });
    });
    const Outcome per_element_region = increment_then_triple(rt,
        [](auto& graph, auto& target, Word* words)
    {
        graph.add(WIDTH, API::Reads(target), API::Writes(target),
                  [words](std::size_t i) { words[i] += 1u; });
    });
    expect_same(per_element, per_element_region);

    const Outcome dynamic = increment_then_triple(rt, [](auto& graph, auto&, Word* words)
    {
        graph.add(API::sized([] { return WIDTH; }), std::vector<void*>{ words },
                  std::vector<void*>{ words }, WIDTH, [words](std::size_t i) { words[i] += 1u; });
    });
    const Outcome dynamic_region = increment_then_triple(rt,
        [](auto& graph, auto& target, Word* words)
    {
        graph.add(API::sized([] { return WIDTH; }), WIDTH, API::Reads(target), API::Writes(target),
                  [words](std::size_t i) { words[i] += 1u; });
    });
    expect_same(dynamic, dynamic_region);

    const Outcome offload = increment_then_triple(rt, [](auto& graph, auto&, Word* words)
    {
        graph.add("builder.increment"_op, WIDTH, std::vector<void*>{ words },
                  std::vector<void*>{ words }, [words](std::size_t i) { words[i] += 1u; });
    });
    const Outcome offload_region = increment_then_triple(rt,
        [](auto& graph, auto& target, Word* words)
    {
        graph.add("builder.increment"_op, WIDTH, API::Reads(target), API::Writes(target),
                  [words](std::size_t i) { words[i] += 1u; });
    });
    expect_same(offload, offload_region);
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
