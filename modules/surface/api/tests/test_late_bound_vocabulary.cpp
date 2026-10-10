/**************************************************************************/
/* test_late_bound_vocabulary.cpp                                         */
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
 * @file test_late_bound_vocabulary.cpp
 * @brief Tests what LateCount and Dynamic let a caller write and what they reject when built.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::uint32_t CANARY = 0xDEADBEEFu;

    /** @brief Detects a data member named capacity that a caller can assign. */
    template <typename T, typename = void>
    struct HasAssignableCapacity : std::false_type
    {
    };

    template <typename T>
    struct HasAssignableCapacity<
        T, std::void_t<decltype(std::declval<T&>().capacity = std::size_t{})>> : std::true_type
    {
    };

    /** @brief Detects a public member named when_pred. */
    template <typename T, typename = void>
    struct HasPublicWhenPredicate : std::false_type
    {
    };

    template <typename T>
    struct HasPublicWhenPredicate<T, std::void_t<decltype(std::declval<T&>().when_pred)>>
        : std::true_type
    {
    };

    /** @brief Detects a size setter that takes a bare pointer and an index. */
    template <typename T, typename = void>
    struct HasRawPointerSizeSetter : std::false_type
    {
    };

    template <typename T>
    struct HasRawPointerSizeSetter<
        T, std::void_t<decltype(std::declval<T&>().and_sized_from_device(
               std::declval<const std::uint32_t*>(), std::size_t{}))>> : std::true_type
    {
    };

    /** @brief Detects a size setter that takes a bare pointer under the new name. */
    template <typename T, typename = void>
    struct HasRawPointerSizedFromDevice : std::false_type
    {
    };

    template <typename T>
    struct HasRawPointerSizedFromDevice<
        T, std::void_t<decltype(std::declval<T&>().sized_from_device(
               std::declval<const std::uint32_t*>(), std::size_t{}))>> : std::true_type
    {
    };

    static_assert(!std::is_default_constructible_v<LateCount>,
                  "a LateCount is built by fixed(), from_host() or from_device() only");
    static_assert(std::is_copy_constructible_v<LateCount>,
                  "Recorder::resolve() takes its counts in a std::array, by copy");
    static_assert(!HasAssignableCapacity<LateCount>::value,
                  "the capacity of a LateCount is set when it is built");
    static_assert(std::is_same_v<LateCount::Provider, API::Dynamic::CountProvider>,
                  "LateCount and Dynamic::sized() take one provider type");
    static_assert(!HasPublicWhenPredicate<API::Dynamic>::value,
                  "the controls of a Dynamic are private");
    static_assert(!HasRawPointerSizeSetter<API::Dynamic>::value,
                  "and_sized_from_device(pointer, index) is removed");
    static_assert(!HasRawPointerSizedFromDevice<API::Dynamic>::value,
                  "sized_from_device() takes the Buffer, never a bare pointer");
    static_assert(std::is_nothrow_move_constructible_v<API::Dynamic> &&
                      !std::is_copy_constructible_v<API::Dynamic>,
                  "a Dynamic moves and does not copy");

    /** @brief Runs a call and returns the error of the SushiException it throws, or code ok. */
    template <class Call>
    Core::Error error_of(Call&& call)
    {
        try
        {
            call();
        }
        catch (const SushiException& e)
        {
            return e.error();
        }
        return Core::Error{};
    }

    /**
     * @brief Records the resolver of one late count; its fill copies the live value to out[0].
     * @param out Receives the live count; must outlive the graph.
     */
    void record_live_count_copy(API::Recorder& recorder, const LateCount& count,
                                const API::Buffer<std::uint32_t>& out)
    {
        std::uint32_t* target = out.data();
        recorder.resolve(std::array<LateCount, 1>{count}, out.device_index(),
                         API::Access(Reads(), Writes(out)),
                         [target](const std::array<std::size_t, 1>& live)
                         {
                             target[0] = static_cast<std::uint32_t>(live[0]);
                         });
    }

    /**
     * @brief Adds a task of the given capacity that sets marks[i] to one, and runs the graph once.
     * @param marks Holds at least capacity elements, all zero before the call.
     * @return How many elements the task set.
     */
    std::size_t count_elements_run(Runtime& rt, API::Dynamic window, API::Buffer<int>& marks,
                                   std::size_t capacity)
    {
        int* m = marks.data();
        auto g = rt.graph();
        g.add(std::move(window), capacity, Reads{}, Writes(marks),
              [m](std::size_t i) { m[i] = 1; });
        g.run();

        std::size_t ran = 0;
        for (std::size_t i = 0; i < capacity; ++i)
            ran += static_cast<std::size_t>(marks[i]);
        return ran;
    }
} // namespace

/** @brief Tests that from_device rejects an index one past its slot when the count is built. */
TEST(Integration_LateBoundVocabulary, FromDeviceRejectsAnIndexPastTheSlot)
{
    constexpr std::size_t CAPACITY = 64;

    auto rt   = Runtime::create();
    auto slot = rt.buffer<std::uint32_t>(3, DeviceIndex{0}, Residency::Host);

    const Core::Error error =
        error_of([&] { (void)LateCount::from_device(CAPACITY, slot, slot.size()); });

    EXPECT_EQ(error.code, Core::Errc::invalid_access);
    EXPECT_NE(error.message.find("LateCount::from_device"), std::string::npos) << error.message;
    EXPECT_NE(error.message.find("index 3"), std::string::npos) << error.message;
    EXPECT_NE(error.message.find("3 elements"), std::string::npos) << error.message;

    EXPECT_NO_THROW((void)LateCount::from_device(CAPACITY, slot, slot.size() - 1));
}

/** @brief Tests that from_device rejects a slot the host cannot read during a run. */
TEST(Integration_LateBoundVocabulary, FromDeviceRejectsASlotThatIsNotHostResident)
{
    auto rt       = Runtime::create();
    auto resident = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device);
    auto shared   = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Shared);

    for (const Buffer<std::uint32_t>* rejected : {&resident, &shared})
    {
        const Core::Error error = error_of([&] { (void)LateCount::from_device(8, *rejected); });
        EXPECT_EQ(error.code, Core::Errc::invalid_access);
        EXPECT_NE(error.message.find("Residency::Host"), std::string::npos) << error.message;
    }
}

/** @brief Tests that from_host rejects a provider that holds no callable. */
TEST(Integration_LateBoundVocabulary, FromHostRejectsAnEmptyProvider)
{
    const Core::Error error =
        error_of([] { (void)LateCount::from_host(8, LateCount::Provider{}); });

    EXPECT_EQ(error.code, Core::Errc::invalid_access);
    EXPECT_NE(error.message.find("LateCount::from_host"), std::string::npos) << error.message;
}

/** @brief Tests that every factory rejects a capacity a uint32 live-count word cannot hold. */
TEST(Integration_LateBoundVocabulary, EveryFactoryRejectsACapacityPastAUint32)
{
    if (sizeof(std::size_t) <= sizeof(std::uint32_t))
        GTEST_SKIP() << "std::size_t cannot hold a count past a uint32 on this target";

    const std::size_t limit = std::numeric_limits<std::uint32_t>::max();
    const std::size_t over = limit + 1;

    auto rt   = Runtime::create();
    auto slot = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);

    EXPECT_EQ(error_of([&] { (void)LateCount::fixed(over); }).code, Core::Errc::invalid_graph);
    EXPECT_EQ(error_of([&] {
                  (void)LateCount::from_host(over, [] { return std::size_t{0}; });
              }).code,
              Core::Errc::invalid_graph);
    EXPECT_EQ(error_of([&] { (void)LateCount::from_device(over, slot); }).code,
              Core::Errc::invalid_graph);

    EXPECT_EQ(LateCount::fixed(limit).capacity(), limit);
}

/** @brief Tests that capacity() reports the plan size each factory was given. */
TEST(Integration_LateBoundVocabulary, CapacityReportsThePlanSize)
{
    auto rt   = Runtime::create();
    auto slot = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);

    EXPECT_EQ(LateCount::fixed(42).capacity(), 42u);
    EXPECT_EQ(LateCount::from_host(700, [] { return std::size_t{1}; }).capacity(), 700u);
    EXPECT_EQ(LateCount::from_device(900, slot).capacity(), 900u);
}

/** @brief Tests that a recorded copy still polls the provider after the original is destroyed. */
TEST(Integration_LateBoundVocabulary, ARecordedCopyKeepsTheHostProvider)
{
    auto rt  = Runtime::create();
    auto out = rt.buffer<std::uint32_t>(1);
    out[0] = CANARY;

    std::size_t live = 11;
    auto g = rt.graph();
    {
        const LateCount original = LateCount::from_host(100, [&live] { return live; });
        const LateCount copy = original;
        record_live_count_copy(g.recorder(), copy, out);
    }

    g.run();
    EXPECT_EQ(out[0], 11u);

    live = 93;
    g.run();
    EXPECT_EQ(out[0], 93u) << "the copy must poll the provider on every step";
    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Tests that from_device reads the word its index names, not the first word. */
TEST(Integration_LateBoundVocabulary, FromDeviceReadsTheWordAtItsIndex)
{
    auto rt   = Runtime::create();
    auto slot = rt.buffer<std::uint32_t>(4, DeviceIndex{0}, Residency::Host);
    auto out  = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < 4; ++i)
        slot[i] = 5;
    slot[2] = 77;
    out[0] = CANARY;

    auto g = rt.graph();
    record_live_count_copy(g.recorder(), LateCount::from_device(100, slot, 2), out);
    g.run();

    EXPECT_EQ(out[0], 77u);
}

/** @brief Tests that the size source set last decides the live size, whichever kind it is. */
TEST(Integration_LateBoundVocabulary, TheSizeSourceSetLastWins)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::uint32_t DEVICE_COUNT = 3;
    constexpr std::size_t HOST_COUNT = 5;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto marks   = rt.buffer<int>(CAPACITY);
    counter[0] = DEVICE_COUNT;

    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = 0;
    EXPECT_EQ(count_elements_run(rt,
                                 API::sized_from_device(counter).sized([] { return HOST_COUNT; }),
                                 marks, CAPACITY),
              HOST_COUNT)
        << "sized() after sized_from_device() must drop the device slot";

    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = 0;
    EXPECT_EQ(count_elements_run(rt,
                                 API::sized([] { return HOST_COUNT; }).sized_from_device(counter),
                                 marks, CAPACITY),
              std::size_t{DEVICE_COUNT})
        << "sized_from_device() after sized() must drop the host provider";
}

/** @brief Tests that the members chain on a named Dynamic and carry all three controls. */
TEST(Integration_LateBoundVocabulary, MembersChainOnANamedDynamic)
{
    constexpr std::size_t CAPACITY = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAPACITY);
    int* p = buf.data();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        buf[i] = 0;

    bool enabled = true;
    API::Dynamic window;
    window.when([&enabled] { return enabled; })
        .sized([] { return std::size_t{4}; })
        .based_at([] { return std::size_t{10}; });

    auto g = rt.graph();
    g.add(std::move(window), CAPACITY, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });
    g.run();

    for (std::size_t i = 0; i < CAPACITY; ++i)
        ASSERT_EQ(buf[i], (i >= 10 && i < 14) ? 1 : 0) << "element " << i;

    enabled = false;
    g.run();
    EXPECT_EQ(buf[10], 1) << "a disabled step must run no element";
}

/** @brief Tests that and_when, and_sized and and_based_at set what their replacements set. */
TEST(Integration_LateBoundVocabulary, DeprecatedSettersForwardToTheirReplacements)
{
    constexpr std::size_t CAPACITY = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAPACITY);
    int* p = buf.data();
    for (std::size_t i = 0; i < CAPACITY; ++i)
        buf[i] = 0;

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    API::Dynamic window = API::Dynamic()
                              .and_when([] { return true; })
                              .and_sized([] { return std::size_t{6}; })
                              .and_based_at([] { return std::size_t{20}; });
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    auto g = rt.graph();
    g.add(std::move(window), CAPACITY, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = 1; });
    g.run();

    for (std::size_t i = 0; i < CAPACITY; ++i)
        ASSERT_EQ(buf[i], (i >= 20 && i < 26) ? 1 : 0) << "element " << i;
}
