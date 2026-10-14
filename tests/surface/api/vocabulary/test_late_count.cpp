/**************************************************************************/
/* test_late_count.cpp                                                    */
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
 * @file test_late_count.cpp
 * @brief Tests LateCount and its host and device factories.
 * @see tests/surface/api/vocabulary/README.md
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

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Tests where Recorder::resolve() takes a LateCount's live value from, and when. */
namespace LateCountResolutionTests
{
    namespace
    {
        constexpr std::uint32_t K_CANARY = 0xDEADBEEFu;

        /**
         * @brief Records the resolver of one late count; its fill copies the live value to out[0].
         * @param out Receives the live count; must outlive the graph.
         */
        void record_live_count_copy(Recorder& recorder, const LateCount& count,
                                    const Buffer<std::uint32_t>& out)
        {
            std::uint32_t* target = out.data();
            recorder.resolve(std::array<LateCount, 1>{count}, out.device_index(),
                             Access(Reads(), Writes(out)),
                             [target](const std::array<std::size_t, 1>& live)
                             {
                                 target[0] = static_cast<std::uint32_t>(live[0]);
                             });
        }

        /** @brief Returns a LateCount of the given capacity that reads its live value from slot[0]. */
        LateCount device_count(const Buffer<std::uint32_t>& slot, std::size_t capacity)
        {
            return LateCount::from_device(capacity, slot);
        }
    } // namespace

    /** @brief Tests that a fixed LateCount resolves to the count it was built with. */
    TEST(Integration_LateCount, FixedResolvesToItsCount)
    {
        auto rt  = Runtime::create().value();
        auto out = rt.buffer<std::uint32_t>(1).value();
        out[0] = K_CANARY;

        const LateCount count = LateCount::fixed(42);

        auto g = rt.graph();
        record_live_count_copy(g.recorder(), count, out);
        g.run().value();

        EXPECT_EQ(out[0], 42u);
    }

    /** @brief Tests that a host provider below capacity is the live count. */
    TEST(Integration_LateCount, HostProviderBelowCapacity)
    {
        auto rt  = Runtime::create().value();
        auto out = rt.buffer<std::uint32_t>(1).value();
        out[0] = K_CANARY;

        const LateCount count =
            LateCount::from_host(1000000, []() { return std::size_t{700000}; });

        auto g = rt.graph();
        record_live_count_copy(g.recorder(), count, out);
        g.run().value();

        EXPECT_EQ(out[0], 700000u);
    }

    /** @brief Tests that a host count that changes between runs keeps one compile. */
    TEST(Integration_LateCount, ChangingHostCountKeepsOneCompile)
    {
        auto rt  = Runtime::create().value();
        auto out = rt.buffer<std::uint32_t>(1).value();
        out[0] = K_CANARY;

        std::size_t live = 1000;
        const LateCount count = LateCount::from_host(700000, [&live]() { return live; });

        auto g = rt.graph();
        record_live_count_copy(g.recorder(), count, out);

        g.run().value();
        EXPECT_EQ(out[0], 1000u);

        live = 700000;
        g.run().value();
        EXPECT_EQ(out[0], 700000u);

        live = 0;
        g.run().value();
        EXPECT_EQ(out[0], 0u);

        EXPECT_EQ(g.compile_count(), 1u)
            << "a live count change between runs must not trigger a recompile";
    }

    /** @brief Tests that a host count above its capacity fails the run and a later run goes on. */
    TEST(Integration_LateCount, HostCountAboveCapacityFailsTheRunWithInvalidGraph)
    {
        auto rt  = Runtime::create().value();
        auto out = rt.buffer<std::uint32_t>(1).value();
        out[0] = K_CANARY;

        std::size_t live = 101;
        const LateCount count = LateCount::from_host(100, [&live]() { return live; });

        auto g = rt.graph();
        record_live_count_copy(g.recorder(), count, out);

        const auto failed = g.run();
        ASSERT_FALSE(failed.has_value());
        EXPECT_EQ(failed.error().code, ErrorCode::InvalidGraph);
        EXPECT_NE(failed.error().message.find("the live count (101) exceeds the capacity"),
                  std::string::npos)
            << failed.error().message;
        EXPECT_EQ(out[0], 0u) << "a rejected count reaches the fill as zero, as the device path does";

        // The process goes on: the same graph runs once the provider returns a count that fits.
        live = 100;
        const auto valid = g.run();
        ASSERT_TRUE(valid.has_value()) << valid.error().message;
        EXPECT_EQ(out[0], 100u);
    }

    /** @brief Tests that a Residency::Host device slot resolves to the value written there. */
    TEST(Integration_LateCount, DeviceSlotResolvesFromHostResidentBuffer)
    {
        auto rt      = Runtime::create().value();
        auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
        auto out     = rt.buffer<std::uint32_t>(1).value();
        counter[0] = 700000;
        out[0] = K_CANARY;

        auto g = rt.graph();
        record_live_count_copy(g.recorder(), device_count(counter, 1000000), out);
        g.run().value();

        EXPECT_EQ(out[0], 700000u);
    }

    /** @brief Tests that a device count above its capacity fails the run on both layers. */
    TEST(Integration_LateCount, DeviceCountAboveCapacityFailsTheRunOnBothLayers)
    {
        for (const bool force_scheduler : {false, true})
        {
            RuntimeConfig config;
            config.force_scheduler = force_scheduler;
            auto rt      = Runtime::create(config).value();
            auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
            auto out     = rt.buffer<std::uint32_t>(1).value();
            counter[0] = 101;
            out[0] = K_CANARY;

            auto g = rt.graph();
            record_live_count_copy(g.recorder(), device_count(counter, 100), out);
            EXPECT_FALSE(g.run().has_value()) << "force_scheduler = " << force_scheduler;

            // A later graph with a count inside the capacity runs clean on the same runtime.
            counter[0] = 100;
            auto valid = rt.graph();
            record_live_count_copy(valid.recorder(), device_count(counter, 100), out);
            const auto report = valid.run().value();
            EXPECT_TRUE(report.is_successful) << "force_scheduler = " << force_scheduler;
            EXPECT_EQ(out[0], 100u) << "force_scheduler = " << force_scheduler;
        }
    }

    /** @brief Tests that a device slot outside Residency::Host stops the call that builds the count. */
    TEST(Integration_LateCountDeathTest, ANonHostDeviceSlotStopsAtConstruction)
    {
        auto rt      = Runtime::create().value();
        auto counter = rt.buffer<std::uint32_t>(1).value();

        EXPECT_DEATH(static_cast<void>(LateCount::from_device(16, counter)),
                     "LateCount::from_device: the buffer is Residency::Shared");
    }

    /** @brief Tests that the resolver sees the count its predecessor wrote in the same run. */
    TEST(Integration_LateCount, ResolverSeesTheCountItsPredecessorJustWrote)
    {
        constexpr std::size_t CAPACITY = 1000;

        auto rt      = Runtime::create().value();
        auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
        auto source  = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
        auto out     = rt.buffer<std::uint32_t>(1).value();
        counter[0] = 0;
        out[0] = K_CANARY;

        // A device kernel cannot reach this frame, so its next count is in a buffer; see README.md.
        const std::uint32_t* source_ptr = source.data();
        std::uint32_t* counter_ptr = counter.data();

        auto g = rt.graph();
        g.add(std::size_t{1}, Reads(source), Writes(counter),
              [counter_ptr, source_ptr](std::size_t) { counter_ptr[0] = source_ptr[0]; });
        record_live_count_copy(g.recorder(), device_count(counter, CAPACITY), out);

        source[0] = 300;
        g.run().value();
        EXPECT_EQ(out[0], 300u) << "the resolver must read the count its own step's producer wrote";

        source[0] = 700;
        g.run().value();
        EXPECT_EQ(out[0], 700u) << "a changed count must take effect the same run, not one run late";

        EXPECT_EQ(g.compile_count(), 1u);
    }
} // namespace LateCountResolutionTests

/** @brief Tests what LateCount and Dynamic let a caller write and what they reject when built. */
namespace LateBoundVocabularyTests
{
    namespace
    {
        constexpr std::uint32_t K_CANARY = 0xDEADBEEFu;

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
        static_assert(std::is_same_v<LateCount::Provider, Dynamic::CountProvider>,
                      "LateCount and Dynamic::sized() take one provider type");
        static_assert(!HasPublicWhenPredicate<Dynamic>::value,
                      "the controls of a Dynamic are private");
        static_assert(!HasRawPointerSizeSetter<Dynamic>::value,
                      "and_sized_from_device(pointer, index) is removed");
        static_assert(!HasRawPointerSizedFromDevice<Dynamic>::value,
                      "sized_from_device() takes the Buffer, never a bare pointer");
        static_assert(std::is_nothrow_move_constructible_v<Dynamic> &&
                          !std::is_copy_constructible_v<Dynamic>,
                      "a Dynamic moves and does not copy");

        /**
         * @brief Records the resolver of one late count; its fill copies the live value to out[0].
         * @param out Receives the live count; must outlive the graph.
         */
        void record_live_count_copy(Recorder& recorder, const LateCount& count,
                                    const Buffer<std::uint32_t>& out)
        {
            std::uint32_t* target = out.data();
            recorder.resolve(std::array<LateCount, 1>{count}, out.device_index(),
                             Access(Reads(), Writes(out)),
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
        std::size_t count_elements_run(Runtime& rt, Dynamic window, Buffer<int>& marks,
                                       std::size_t capacity)
        {
            int* m = marks.data();
            auto g = rt.graph();
            g.add_dynamic(std::move(window), capacity, Reads{}, Writes(marks),
                          [m](std::size_t i) { m[i] = 1; });
            g.run().value();

            std::size_t ran = 0;
            for (std::size_t i = 0; i < capacity; ++i)
                ran += static_cast<std::size_t>(marks[i]);
            return ran;
        }
    } // namespace

    /** @brief Tests that from_device stops on an index one past its slot when the count is built. */
    TEST(Integration_LateBoundVocabularyDeathTest, FromDeviceStopsOnAnIndexPastTheSlot)
    {
        constexpr std::size_t CAPACITY = 64;

        auto rt   = Runtime::create().value();
        auto slot = rt.buffer<std::uint32_t>(3, DeviceIndex{0}, Residency::Host).value();

        EXPECT_DEATH(static_cast<void>(LateCount::from_device(CAPACITY, slot, slot.size())),
                     "LateCount::from_device: index 3 is out of range for a buffer of 3 elements");

        const LateCount last = LateCount::from_device(CAPACITY, slot, slot.size() - 1);
        EXPECT_EQ(last.capacity(), CAPACITY);
    }

    /** @brief Tests that from_device stops on a slot the host cannot read during a run. */
    TEST(Integration_LateBoundVocabularyDeathTest, FromDeviceStopsOnASlotThatIsNotHostResident)
    {
        auto rt       = Runtime::create().value();
        auto resident = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device).value();
        auto shared   = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Shared).value();

        EXPECT_DEATH(static_cast<void>(LateCount::from_device(8, resident)),
                     "allocate it with Residency::Host");
        EXPECT_DEATH(static_cast<void>(LateCount::from_device(8, shared)),
                     "allocate it with Residency::Host");
    }

    /** @brief Tests that from_host stops on a provider that holds no callable. */
    TEST(Integration_LateBoundVocabularyDeathTest, FromHostStopsOnAnEmptyProvider)
    {
        EXPECT_DEATH(static_cast<void>(LateCount::from_host(8, LateCount::Provider{})),
                     "LateCount::from_host: the provider is empty");
    }

    /** @brief Tests that every factory stops on a capacity a uint32 live-count word cannot hold. */
    TEST(Integration_LateBoundVocabularyDeathTest, EveryFactoryStopsOnACapacityPastAUint32)
    {
        if (sizeof(std::size_t) <= sizeof(std::uint32_t))
            GTEST_SKIP() << "std::size_t cannot hold a count past a uint32 on this target";

        const std::size_t limit = std::numeric_limits<std::uint32_t>::max();
        const std::size_t over = limit + 1;
        const std::string pattern = "capacity " + std::to_string(over) + " exceeds";

        auto rt   = Runtime::create().value();
        auto slot = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();

        EXPECT_DEATH(static_cast<void>(LateCount::fixed(over)), "LateCount::fixed: " + pattern);
        EXPECT_DEATH(
            static_cast<void>(LateCount::from_host(over, [] { return std::size_t{0}; })),
            "LateCount::from_host: " + pattern);
        EXPECT_DEATH(static_cast<void>(LateCount::from_device(over, slot)),
                     "LateCount::from_device: " + pattern);

        EXPECT_EQ(LateCount::fixed(limit).capacity(), limit);
    }

    /** @brief Tests that capacity() reports the plan size each factory was given. */
    TEST(Integration_LateBoundVocabulary, CapacityReportsThePlanSize)
    {
        auto rt   = Runtime::create().value();
        auto slot = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();

        EXPECT_EQ(LateCount::fixed(42).capacity(), 42u);
        EXPECT_EQ(LateCount::from_host(700, [] { return std::size_t{1}; }).capacity(), 700u);
        EXPECT_EQ(LateCount::from_device(900, slot).capacity(), 900u);
    }

    /** @brief Tests that a recorded copy still polls the provider after the original is destroyed. */
    TEST(Integration_LateBoundVocabulary, ARecordedCopyKeepsTheHostProvider)
    {
        auto rt  = Runtime::create().value();
        auto out = rt.buffer<std::uint32_t>(1).value();
        out[0] = K_CANARY;

        std::size_t live = 11;
        auto g = rt.graph();
        {
            const LateCount original = LateCount::from_host(100, [&live] { return live; });
            const LateCount copy = original;
            record_live_count_copy(g.recorder(), copy, out);
        }

        g.run().value();
        EXPECT_EQ(out[0], 11u);

        live = 93;
        g.run().value();
        EXPECT_EQ(out[0], 93u) << "the copy must poll the provider on every step";
        EXPECT_EQ(g.compile_count(), 1u);
    }

    /** @brief Tests that from_device reads the word its index names, not the first word. */
    TEST(Integration_LateBoundVocabulary, FromDeviceReadsTheWordAtItsIndex)
    {
        auto rt   = Runtime::create().value();
        auto slot = rt.buffer<std::uint32_t>(4, DeviceIndex{0}, Residency::Host).value();
        auto out  = rt.buffer<std::uint32_t>(1).value();
        for (std::size_t i = 0; i < 4; ++i)
            slot[i] = 5;
        slot[2] = 77;
        out[0] = K_CANARY;

        auto g = rt.graph();
        record_live_count_copy(g.recorder(), LateCount::from_device(100, slot, 2), out);
        g.run().value();

        EXPECT_EQ(out[0], 77u);
    }

    /** @brief Tests that the size source set last decides the live size, whichever kind it is. */
    TEST(Integration_LateBoundVocabulary, TheSizeSourceSetLastWins)
    {
        constexpr std::size_t CAPACITY = 16;
        constexpr std::uint32_t DEVICE_COUNT = 3;
        constexpr std::size_t HOST_COUNT = 5;

        auto rt      = Runtime::create().value();
        auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
        auto marks   = rt.buffer<int>(CAPACITY).value();
        counter[0] = DEVICE_COUNT;

        for (std::size_t i = 0; i < CAPACITY; ++i)
            marks[i] = 0;
        EXPECT_EQ(count_elements_run(rt,
                                     sized_from_device(counter).sized([] { return HOST_COUNT; }),
                                     marks, CAPACITY),
                  HOST_COUNT)
            << "sized() after sized_from_device() must drop the device slot";

        for (std::size_t i = 0; i < CAPACITY; ++i)
            marks[i] = 0;
        EXPECT_EQ(count_elements_run(rt,
                                     sized([] { return HOST_COUNT; }).sized_from_device(counter),
                                     marks, CAPACITY),
                  std::size_t{DEVICE_COUNT})
            << "sized_from_device() after sized() must drop the host provider";
    }

    /** @brief Tests that the members chain on a named Dynamic and carry all three controls. */
    TEST(Integration_LateBoundVocabulary, MembersChainOnANamedDynamic)
    {
        constexpr std::size_t CAPACITY = 32;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(CAPACITY).value();
        int* p = buf.data();
        for (std::size_t i = 0; i < CAPACITY; ++i)
            buf[i] = 0;

        bool enabled = true;
        Dynamic window;
        window.when([&enabled] { return enabled; })
            .sized([] { return std::size_t{4}; })
            .based_at([] { return std::size_t{10}; });

        auto g = rt.graph();
        g.add_dynamic(std::move(window), CAPACITY, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });
        g.run().value();

        for (std::size_t i = 0; i < CAPACITY; ++i)
            ASSERT_EQ(buf[i], (i >= 10 && i < 14) ? 1 : 0) << "element " << i;

        enabled = false;
        g.run().value();
        EXPECT_EQ(buf[10], 1) << "a disabled step must run no element";
    }
} // namespace LateBoundVocabularyTests
