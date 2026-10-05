/****************************************************************/
/* test_device_count.cpp                                        */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_device_count.cpp
 * @brief Tests API::sized_from_device, which sizes a task from a count written earlier in the step.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/** @brief Tests that a consumer iterates exactly the count the producer wrote in the same run(). */
TEST(Integration_DeviceCount, ConsumerIteratesTheCountTheProducerJustWrote)
{
    constexpr std::size_t CAPACITY = 4096;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto marks   = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* m = marks.data();

    // The producer reads the count through a pointer, so the device computes it; see README.md.
    auto wanted = rt.buffer<std::uint32_t>(1);
    std::uint32_t* w = wanted.data();

    counter[0] = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        marks[i] = -1;

    auto g = rt.graph();

    // Producer: derive the count on the device from data the kernel reads.
    g.add(Reads(wanted), Writes(counter), std::size_t{1},
          [c, w](std::size_t) { c[0] = w[0] * 3u; });

    // Consumer: iterate exactly that many elements.
    g.add(API::sized_from_device(counter), Reads(), Writes(marks), CAPACITY,
          [m](std::size_t i) { m[i] = static_cast<int>(i); });

    const std::uint32_t multipliers[] = { 5, 200, 1, 0, 1000 };
    for (std::uint32_t multiplier : multipliers)
    {
        wanted[0] = multiplier;
        for (std::size_t i = 0; i < CAPACITY; ++i)
            marks[i] = -1;

        g.run();

        const std::size_t expected = static_cast<std::size_t>(multiplier) * 3u;
        ASSERT_EQ(counter[0], multiplier * 3u);

        for (std::size_t i = 0; i < expected; ++i)
            ASSERT_EQ(marks[i], static_cast<int>(i))
                << "element " << i << " should have run for count " << expected;
        for (std::size_t i = expected; i < CAPACITY; ++i)
            ASSERT_EQ(marks[i], -1)
                << "element " << i << " ran past the live count " << expected;
    }

    EXPECT_EQ(g.compile_count(), 1u)
        << "a device-driven count must not force a recompile";
}

/** @brief Tests that the counter is registered as a read of the consuming task. */
TEST(Integration_DeviceCount, CounterIsRegisteredAsAReadAutomatically)
{
    constexpr std::size_t CAPACITY = 512;
    constexpr std::uint32_t LIVE = 300;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto out     = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* o = out.data();

    counter[0] = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        out[i] = 0;

    auto g = rt.graph();
    g.add(Reads(), Writes(counter), std::size_t{1},
          [c](std::size_t) { c[0] = LIVE; });
    g.add(API::sized_from_device(counter), Reads(), Writes(out), CAPACITY,
          [o](std::size_t i) { o[i] = 1; });

    g.run();

    int ran = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i)
        ran += out[i];
    EXPECT_EQ(ran, static_cast<int>(LIVE))
        << "the consumer did not observe the count written this step";
}

/** @brief Tests a three-stage chain where each stage's output count sizes the next stage. */
TEST(Integration_DeviceCount, ChainedProducerCountsInOneComposition)
{
    constexpr std::size_t CAPACITY = 1024;

    auto rt     = Runtime::create();
    auto first  = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto second = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto stage1 = rt.buffer<int>(CAPACITY);
    auto stage2 = rt.buffer<int>(CAPACITY);

    std::uint32_t* c1 = first.data();
    std::uint32_t* c2 = second.data();
    int* s1 = stage1.data();
    int* s2 = stage2.data();

    first[0] = 0;
    second[0] = 0;

    auto g = rt.graph();

    // Stage 0: pick a first-level count on the device.
    g.add(Reads(), Writes(first), std::size_t{1}, [c1](std::size_t) { c1[0] = 700u; });

    // Stage 1: iterate it, and produce the next count from how much it did.
    g.add(API::sized_from_device(first), Reads(), Writes(stage1), CAPACITY,
          [s1](std::size_t i) { s1[i] = 1; });
    g.add(Reads(first), Writes(second), std::size_t{1},
          [c1, c2](std::size_t) { c2[0] = c1[0] / 2u; });

    // Stage 2: iterate the second count.
    g.add(API::sized_from_device(second), Reads(), Writes(stage2), CAPACITY,
          [s2](std::size_t i) { s2[i] = 1; });

    for (std::size_t i = 0; i < CAPACITY; ++i) { stage1[i] = 0; stage2[i] = 0; }
    g.run();

    int ran1 = 0, ran2 = 0;
    for (std::size_t i = 0; i < CAPACITY; ++i) { ran1 += stage1[i]; ran2 += stage2[i]; }

    EXPECT_EQ(ran1, 700);
    EXPECT_EQ(ran2, 350);
    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Tests that a disabled when() step runs nothing whatever the counter says. */
TEST(Integration_DeviceCount, WhenComposesWithDeviceSize)
{
    constexpr std::size_t CAPACITY = 256;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto out     = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* o = out.data();
    counter[0] = 0;

    bool enabled = true;

    auto g = rt.graph();
    g.add(Reads(), Writes(counter), std::size_t{1}, [c](std::size_t) { c[0] = 100u; });
    g.add(API::sized_from_device(counter)
              .and_when([&enabled] { return enabled; }),
          Reads(), Writes(out), CAPACITY,
          [o](std::size_t i) { o[i] += 1; });

    for (std::size_t i = 0; i < CAPACITY; ++i) o[i] = 0;

    g.run();
    EXPECT_EQ(out[0], 1);

    enabled = false;
    g.run();
    EXPECT_EQ(out[0], 1) << "a disabled step must run no elements";

    enabled = true;
    g.run();
    EXPECT_EQ(out[0], 2);
}

/**
 * @brief Tests that a count above the compiled capacity fails the run with a message naming both
 *        numbers.
 */
TEST(Integration_DeviceCount, CountOverCapacityFailsTheRun)
{
    constexpr std::size_t CAPACITY = 64;

    auto rt      = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    auto out     = rt.buffer<int>(CAPACITY);

    std::uint32_t* c = counter.data();
    int* o = out.data();

    auto g = rt.graph();
    g.add(Reads(), Writes(counter), std::size_t{1},
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    g.add(API::sized_from_device(counter), Reads(), Writes(out), CAPACITY,
          [o](std::size_t i) { o[i] = 1; });

    EXPECT_THROW(g.run(), SushiException);

    // The runtime survives it: the pool is re-armable and a clean graph still runs.
    auto clean = rt.graph();
    clean.add(Reads(), Writes(out), CAPACITY, [o](std::size_t i) { o[i] = 7; });
    clean.run();
    EXPECT_EQ(out[0], 7);
}

/** @brief Tests that a counter the host cannot read is rejected where it is written. */
TEST(Integration_DeviceCount, RejectsACounterThatIsNotHostResident)
{
    auto rt = Runtime::create();
    auto resident = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device);
    auto shared   = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Shared);
    auto host     = rt.buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host);

    for (const Buffer<std::uint32_t>* rejected : { &resident, &shared })
    {
        try
        {
            (void)API::sized_from_device(*rejected);
            ADD_FAILURE() << "a non-Host counter was accepted";
        }
        catch (const SushiException& e)
        {
            EXPECT_EQ(e.error().code, Core::Errc::invalid_access);
            EXPECT_NE(std::string(e.what()).find("Residency::Host"), std::string::npos)
                << e.what();
        }
    }
    EXPECT_THROW(API::sized_from_device(host, 5), SushiException);
    EXPECT_NO_THROW(API::sized_from_device(host, 1));
}

/** @brief Tests that add() resolves an interior pointer to the host allocation it lies in. */
TEST(Integration_DeviceCount, AcceptsAnInteriorPointerIntoAHostSlot)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::size_t SLOT     = 5;

    auto rt   = Runtime::create();
    auto plan = rt.buffer<std::uint32_t>(8, DeviceIndex{0}, Residency::Host);
    auto out  = rt.buffer<int>(CAPACITY);

    for (std::size_t i = 0; i < CAPACITY; ++i)
        out[i] = -1;
    plan[SLOT] = 3;

    int* o = out.data();
    std::uint32_t* interior = plan.data() + SLOT;

    auto g = rt.graph();
    ASSERT_NO_THROW(g.add(API::Dynamic().and_sized_from_device(interior, 0),
                          Reads(), Writes(out), CAPACITY,
                          [o](std::size_t i) { o[i] = static_cast<int>(i); }));
    g.run();

    for (std::size_t i = 0; i < 3; ++i)
        ASSERT_EQ(out[i], static_cast<int>(i)) << "element " << i;
    for (std::size_t i = 3; i < CAPACITY; ++i)
        ASSERT_EQ(out[i], -1) << "element " << i << " ran past the live count";
}

/** @brief Tests that an offset base slot for based_at_device resolves the same way. */
TEST(Integration_DeviceCount, AcceptsAnInteriorPointerIntoAHostBaseSlot)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::size_t SLOT     = 3;

    auto rt   = Runtime::create();
    auto plan = rt.buffer<std::uint32_t>(8, DeviceIndex{0}, Residency::Host);
    auto out  = rt.buffer<int>(CAPACITY);

    plan[SLOT] = 4;
    std::uint32_t* interior = plan.data() + SLOT;

    auto g = rt.graph();
    EXPECT_NO_THROW(g.add(API::Dynamic().and_based_at_device(interior, 0),
                          Reads(), Writes(out), CAPACITY,
                          [](std::size_t) {}));
}

/** @brief Tests that a raw slot that is not host-resident is refused at add(). */
TEST(Integration_DeviceCount, RefusesARawSlotThatIsNotHostResident)
{
    auto rt       = Runtime::create();
    auto resident = rt.buffer<std::uint32_t>(4, DeviceIndex{0}, Residency::Device);
    auto out      = rt.buffer<int>(4);

    std::uint32_t* interior = resident.data() + 1;

    auto g = rt.graph();
    try
    {
        g.add(API::Dynamic().and_sized_from_device(interior, 0),
              Reads(), Writes(out), std::size_t{4}, [](std::size_t) {});
        ADD_FAILURE() << "a device-resident slot was accepted";
    }
    catch (const SushiException& e)
    {
        EXPECT_EQ(e.error().code, Core::Errc::invalid_access);
        const std::string message(e.what());
        EXPECT_NE(message.find("sized_from_device"), std::string::npos) << message;
        EXPECT_NE(message.find("inside an allocation that is not Residency::Host"),
                  std::string::npos) << message;
    }
}

/** @brief Tests that a slot this runtime never allocated is refused with its own message. */
TEST(Integration_DeviceCount, RefusesASlotThisRuntimeNeverAllocated)
{
    auto rt  = Runtime::create();
    auto out = rt.buffer<int>(4);

    std::uint32_t stack_slot = 1;

    auto g = rt.graph();
    try
    {
        g.add(API::Dynamic().and_based_at_device(&stack_slot, 0),
              Reads(), Writes(out), std::size_t{4}, [](std::size_t) {});
        ADD_FAILURE() << "an unregistered slot was accepted";
    }
    catch (const SushiException& e)
    {
        EXPECT_EQ(e.error().code, Core::Errc::invalid_access);
        const std::string message(e.what());
        EXPECT_NE(message.find("based_at_device"), std::string::npos) << message;
        EXPECT_NE(message.find("never allocated"), std::string::npos) << message;
    }
}
