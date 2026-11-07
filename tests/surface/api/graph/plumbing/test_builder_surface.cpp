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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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

#include "support/runtime_environment.hpp"

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
    Buffer<Word> seeded(const Runtime& rt, std::size_t count = K_WIDTH,
                             DeviceIndex device = DeviceIndex{0})
    {
        auto buffer = rt.buffer<Word>(count, device).value();
        for (std::size_t i = 0; i < count; ++i)
            buffer[i] = static_cast<Word>(i) + K_SEED;
        return buffer;
    }

    /** @brief Allocates a State of count words whose current field holds index plus K_SEED. */
    State<Word> seeded_state(const Runtime& rt, std::size_t count = K_WIDTH)
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

/** @brief Fixture of the builder tests, which record graphs on the program's runtime. */
class Medium_BuilderSurface : public TestSupport::RuntimeTest
{
};

/** @brief Tests that a count launch hands its kernel the pointer of each capture. */
TEST_F(Medium_BuilderSurface, CountLaunchWithCapturesPassesPointers)
{
    const Runtime& rt = runtime();
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
TEST_F(Medium_BuilderSurface, NdRangeOriginShiftsTheIDTheKernelReceives)
{
    const Runtime& rt = runtime();
    auto target = seeded(rt);

    const NdRange<1> tiles =
        NdRange<1>(sycl::range<1>(K_PREFIX), sycl::range<1>(K_TILE)).at(sycl::id<1>(K_ORIGIN));

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
TEST_F(Medium_BuilderSurface, StateRuleOverACountUpdatesAPrefix)
{
    const Runtime& rt = runtime();
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
TEST_F(Medium_BuilderSurface, ALaunchPastItsStateIsRefusedAtAdd)
{
    const Runtime& rt = runtime();
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

/** @brief Tests that a whole-field coupled rule reads the State it names. */
TEST_F(Medium_BuilderSurface, AWholeFieldCoupledRuleReadsTheStateItNames)
{
    const Runtime& rt = runtime();
    auto whole = seeded_state(rt);
    auto input = seeded_state(rt);

    auto graph = rt.graph();
    graph.add_coupled(Coupled(whole, input),
                      [](std::size_t i, const Word* current, Word* next, const Word* other)
                      {
                          next[i] = current[i] + other[i];
                      });
    EXPECT_EQ(graph.size(), 1u);
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(whole.current()[i], (static_cast<Word>(i) + K_SEED) * 2u) << "at i=" << i;
}

/** @brief Tests that a launch-shaped coupled rule reads its prefix and the second State. */
TEST_F(Medium_BuilderSurface, ALaunchShapedCoupledRuleReadsItsPrefixAndTheSecondState)
{
    const Runtime& rt = runtime();
    auto prefix = seeded_state(rt);
    auto input = seeded_state(rt);

    auto graph = rt.graph();
    graph.add_coupled(K_PREFIX, Coupled(prefix, input),
                      [](std::size_t i, const Word* current, Word* next, const Word* other)
                      {
                          next[i] = current[i] * other[i];
                      });
    EXPECT_EQ(graph.size(), 1u);
    graph.run().value();

    for (std::size_t i = 0; i < K_PREFIX; ++i)
    {
        const Word seed = static_cast<Word>(i) + K_SEED;
        ASSERT_EQ(prefix.current()[i], seed * seed) << "at i=" << i;
    }
}

/** @brief Tests that a coupled rule whose input is shorter than its output is refused. */
TEST_F(Medium_BuilderSurface, ACoupledInputSmallerThanTheOutputIsRefusedAtAdd)
{
    const Runtime& rt = runtime();
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
TEST_F(Medium_BuilderSurface, DynamicTakesItsCapacityBeforeItsAccess)
{
    const Runtime& rt = runtime();
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

/** @brief Tests that an untracked kernel placed with On(buffer) runs on the device of the buffer named. */
TEST_F(Medium_BuilderSurface, UntrackedKernelRunsOnTheDeviceOfTheBufferNamed)
{
    const Runtime& rt = runtime();
    const DeviceIndex last{rt.advanced().device_count() - 1};
    auto by_handle = seeded(rt, K_WIDTH, last);
    Word* handle_words = by_handle.data();

    auto graph = rt.graph();
    graph.add_untracked(K_WIDTH, On(by_handle),
                        [handle_words](std::size_t i) { handle_words[i] += 1u; });
    EXPECT_EQ(graph.size(), 1u);
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(by_handle[i], static_cast<Word>(i) + K_SEED + 1u) << "at i=" << i;
}

/** @brief Tests that an untracked kernel placed with On(device) runs on the device named. */
TEST_F(Medium_BuilderSurface, UntrackedKernelRunsOnTheDeviceNamed)
{
    const Runtime& rt = runtime();
    const DeviceIndex last{rt.advanced().device_count() - 1};
    auto by_index = seeded(rt, K_WIDTH, last);
    Word* index_words = by_index.data();

    auto graph = rt.graph();
    graph.add_untracked(On(last), [index_words](sycl::handler& handler)
    {
        handler.parallel_for(sycl::range<1>(K_WIDTH),
                             [index_words](sycl::id<1> i) { index_words[i[0]] += 2u; });
    });
    EXPECT_EQ(graph.size(), 1u);
    graph.run().value();

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(by_index[i], static_cast<Word>(i) + K_SEED + 2u) << "at i=" << i;
}

/** @brief Fixture of the death tests, which each start a child process on the program's runtime. */
class Medium_BuilderSurfaceDeathTest : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Returns the tail of the message a device index the runtime lacks leaves. */
        [[nodiscard]] static std::string missing_device_detail(std::size_t count)
        {
            return ": device index " + std::to_string(count) + " names no device; the runtime drives " +
                   std::to_string(count) + " devices";
        }
};

/** @brief Tests that add_untracked(launch, On(device), kernel) stops on a device the runtime has not. */
TEST_F(Medium_BuilderSurfaceDeathTest, ALaunchPlacementOnADeviceTheRuntimeLacksStops)
{
    const std::size_t count = runtime().advanced().device_count();
    auto graph = runtime().graph();

    EXPECT_DEATH(graph.add_untracked(K_WIDTH, On(DeviceIndex{count}), [](std::size_t) {}),
                 "add_untracked\\(launch, On\\(\\.\\.\\.\\), kernel\\)" +
                     missing_device_detail(count));
}

/** @brief Tests that add_untracked(On(device), kernel) stops on a device the runtime has not. */
TEST_F(Medium_BuilderSurfaceDeathTest, AHandlerPlacementOnADeviceTheRuntimeLacksStops)
{
    const std::size_t count = runtime().advanced().device_count();
    auto graph = runtime().graph();

    EXPECT_DEATH(graph.add_untracked(On(DeviceIndex{count}), [](sycl::handler&) {}),
                 "add_untracked\\(On\\(\\.\\.\\.\\), kernel\\)" + missing_device_detail(count));
}

/** @brief Tests that a graph watches the handle an On() names. */
TEST_F(Medium_BuilderSurface, PlacementWatchesTheHandleItNames)
{
    const Runtime& rt = runtime();
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

/** @brief Tests that extend() runs its callable once and counts what it records as one rule of two nodes. */
TEST_F(Medium_BuilderSurface, ExtendRecordsOneNamedRule)
{
    const Runtime& rt = runtime();
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
}

/** @brief Tests that the kernels an extension recorded add their results and the extension is not replayed at a build. */
TEST_F(Medium_BuilderSurface, AnExtensionsKernelsRunAndTheExtensionIsNotReplayed)
{
    const Runtime& rt = runtime();
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

    graph.run().value();
    EXPECT_EQ(calls, 1u) << "an extension is not replayed when the plan is built";

    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(target[i], static_cast<Word>(i) + K_SEED + 2u) << "at i=" << i;
}

/** @brief The add() overloads whose refusal names the call the user wrote, as the rows of one test. */
enum class Overload
{
    StateLaunch,    /**< add_rule(launch, state, kernel) past the State. */
    EmptyState,     /**< add_rule(state, kernel) on an unallocated State. */
    Coupled,        /**< add_coupled(Coupled(...), kernel) with a short input. */
    CoupledLaunch   /**< add_coupled(launch, Coupled(...), kernel) with a short input. */
};

/** @brief Fixture of the rows that refuse one overload each. */
class Medium_BuilderSurfaceRefusals : public TestSupport::RuntimeTest,
                                      public ::testing::WithParamInterface<Overload>
{
};

/** @brief Tests that the refusal of an add() overload names the call the user wrote. */
TEST_P(Medium_BuilderSurfaceRefusals, ARefusalNamesTheCallTheUserWrote)
{
    const Runtime& rt = runtime();
    auto field = seeded_state(rt);
    auto output = seeded_state(rt);
    auto input = seeded_state(rt, K_PREFIX);
    State<Word> unallocated;
    auto graph = rt.graph();
    const Extent<1> window{sycl::range<1>(K_WIDTH), sycl::id<1>(K_ORIGIN)};

    std::string refusal;
    std::string prefix;
    switch (GetParam())
    {
        case Overload::StateLaunch:
            prefix = "add_rule(launch, state, kernel), rule 0: ";
            refusal = refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
            {
                graph.add_rule(window, field, [](sycl::id<1>, const Word*, Word*) {});
            });
            break;
        case Overload::EmptyState:
            prefix = "add_rule(state, kernel), rule 0: ";
            refusal = refusal_of(graph.recorder(), ErrorCode::InvalidAccess, [&]
            {
                graph.add_rule(unallocated, [](std::size_t, const Word*, Word*) {});
            });
            break;
        case Overload::Coupled:
            prefix = "add_coupled(Coupled(out, in0, ...), kernel), rule 0: ";
            refusal = refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
            {
                graph.add_coupled(Coupled(output, input),
                                  [](std::size_t, const Word*, Word*, const Word*) {});
            });
            break;
        case Overload::CoupledLaunch:
            prefix = "add_coupled(launch, Coupled(out, in0, ...), kernel), rule 0: ";
            refusal = refusal_of(graph.recorder(), ErrorCode::InvalidGraph, [&]
            {
                graph.add_coupled(window, Coupled(output, input),
                                  [](sycl::id<1>, const Word*, Word*, const Word*) {});
            });
            break;
    }

    EXPECT_TRUE(begins_with(refusal, prefix)) << refusal;
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}

INSTANTIATE_TEST_SUITE_P(Rows, Medium_BuilderSurfaceRefusals,
                         ::testing::Values(Overload::StateLaunch, Overload::EmptyState,
                                           Overload::Coupled, Overload::CoupledLaunch));

/** @brief Tests that the first refusal is kept, records nothing, and a later one does not replace it. */
TEST_F(Medium_BuilderSurface, TheFirstRefusalIsKept)
{
    const Runtime& rt = runtime();
    auto field = seeded_state(rt);
    auto target = seeded(rt);
    Word* words = target.data();
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

    const std::string first_message = refused->message;
    graph.add_rule(unallocated, [](std::size_t, const Word*, Word*) {});
    ASSERT_NE(graph.refusal(), nullptr);
    EXPECT_EQ(graph.refusal()->message, first_message) << "a later refusal replaced the first";
}

/** @brief Tests that a run returns the kept refusal and runs nothing until clear(). */
TEST_F(Medium_BuilderSurface, ARunReturnsTheRefusalAndRunsNothingUntilCleared)
{
    const Runtime& rt = runtime();
    auto field = seeded_state(rt);
    auto target = seeded(rt);
    Word* words = target.data();
    const Word before = words[0];
    auto graph = rt.graph();
    const Extent<1> window{sycl::range<1>(K_WIDTH), sycl::id<1>(K_ORIGIN)};

    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });
    graph.add_rule(window, field, [](sycl::id<1>, const Word*, Word*) {});
    ASSERT_NE(graph.refusal(), nullptr);

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
