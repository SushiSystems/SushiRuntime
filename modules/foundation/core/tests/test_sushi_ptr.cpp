/**************************************************************************/
/* test_sushi_ptr.cpp                                                     */
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
 * @file test_sushi_ptr.cpp
 * @brief Tests the construction, comparison and ownership rules of sushi_ptr.
 * @see modules/foundation/core/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <type_traits>
#include <utility>
#include <gtest/gtest.h>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

using namespace SushiRuntime;

namespace
{
    /**
     * @brief A reference-counted payload that counts its own destructor calls.
     *
     * Holds a reference to an external counter, incremented when the object is destroyed.
     */
    struct Probe : public Core::RefCounted
    {
        explicit Probe(int& destroyed_count) : destroyed(destroyed_count) {}
        ~Probe() override { ++destroyed; }

        int& destroyed;
    };

    /**
     * @brief A Probe subclass, used to check comparisons against a base-type sushi_ptr.
     */
    struct DerivedProbe : public Probe
    {
        using Probe::Probe;
    };
}

static_assert(!std::is_convertible_v<Probe*, sushi_ptr<Probe>>,
              "A raw pointer must not convert implicitly to sushi_ptr.");
static_assert(std::is_constructible_v<sushi_ptr<Probe>, Probe*>,
              "A raw pointer must construct sushi_ptr explicitly.");
static_assert(std::is_nothrow_constructible_v<sushi_ptr<Probe>, Probe*>,
              "Constructing sushi_ptr from a raw pointer must not throw.");

/**
 * @brief Verifies that comparing with a raw pointer, in either order, leaves the count unchanged.
 *
 * Compares an owner with its own raw pointer and with another object's raw pointer, using const
 * and non-const pointers.
 */
TEST(Unit_SushiPtrTest, RawPointerComparisonKeepsCount)
{
    int destroyed = 0;
    auto owner = make_sushi<Probe>(destroyed);
    auto stranger = make_sushi<Probe>(destroyed);
    Probe* raw = owner.get();
    const Probe* const_raw = raw;
    const int before = owner->get_ref_count();

    EXPECT_TRUE(owner == raw);
    EXPECT_TRUE(raw == owner);
    EXPECT_TRUE(owner == const_raw);
    EXPECT_TRUE(const_raw == owner);
    EXPECT_FALSE(owner != raw);
    EXPECT_FALSE(raw != owner);
    EXPECT_FALSE(owner == stranger.get());
    EXPECT_FALSE(stranger.get() == owner);
    EXPECT_TRUE(owner != stranger.get());
    EXPECT_TRUE(stranger.get() != owner);

    EXPECT_EQ(owner->get_ref_count(), before) << "Comparing with a raw pointer must not copy it.";
    EXPECT_EQ(destroyed, 0);
}

/**
 * @brief Verifies that comparisons with a base-type smart pointer compare the managed objects.
 *
 * Compares a base-type smart pointer with a derived-type one in both orders and asserts the count
 * does not change.
 */
TEST(Unit_SushiPtrTest, BaseTypeComparisonKeepsCount)
{
    int destroyed = 0;
    auto derived = make_sushi<DerivedProbe>(destroyed);
    sushi_ptr<Probe> base = derived;
    const int before = derived->get_ref_count();

    EXPECT_TRUE(base == derived);
    EXPECT_TRUE(derived == base);
    EXPECT_FALSE(base != derived);
    EXPECT_FALSE(derived != base);
    EXPECT_EQ(derived->get_ref_count(), before) << "Comparing across types must not copy either side.";
}

/**
 * @brief Verifies that nullptr comparisons select the nullptr overloads and stay unambiguous.
 *
 * Compares an empty and a non-empty smart pointer with nullptr and with a null raw pointer.
 */
TEST(Unit_SushiPtrTest, NullComparisonsAreUnambiguous)
{
    int destroyed = 0;
    sushi_ptr<Probe> empty;
    auto owner = make_sushi<Probe>(destroyed);

    EXPECT_TRUE(empty == nullptr);
    EXPECT_FALSE(empty != nullptr);
    EXPECT_FALSE(owner == nullptr);
    EXPECT_TRUE(owner != nullptr);
    EXPECT_TRUE(empty == static_cast<const Probe*>(nullptr));
    EXPECT_TRUE(static_cast<const Probe*>(nullptr) == empty);
    EXPECT_FALSE(owner == static_cast<Probe*>(nullptr));
}

/**
 * @brief Verifies that self-assignment and self-move keep the managed object and its count.
 *
 * Assigns a smart pointer to itself through a reference alias, copying then moving, so the
 * compiler does not warn about a direct self-assignment.
 */
TEST(Unit_SushiPtrTest, SelfAssignmentKeepsObject)
{
    int destroyed = 0;
    auto owner = make_sushi<Probe>(destroyed);
    sushi_ptr<Probe>& alias = owner;

    owner = alias;
    EXPECT_TRUE(owner);
    EXPECT_EQ(owner->get_ref_count(), 1) << "Self-copy must net to no change in the count.";
    EXPECT_EQ(destroyed, 0);

    owner = std::move(alias);
    EXPECT_TRUE(owner) << "Self-move must keep the object.";
    EXPECT_EQ(owner->get_ref_count(), 1);
    EXPECT_EQ(destroyed, 0);
}

/**
 * @brief Verifies that reset releases the old object and takes the new one.
 *
 * Resets one owner to another owner's object, then to null, and checks which object is destroyed
 * at each step.
 */
TEST(Unit_SushiPtrTest, ResetReleasesOldAndTakesNew)
{
    int destroyed = 0;
    auto first = make_sushi<Probe>(destroyed);
    auto second = make_sushi<Probe>(destroyed);
    Probe* second_raw = second.get();

    first.reset(second.get());
    EXPECT_EQ(destroyed, 1) << "The object only first held must be destroyed.";
    EXPECT_EQ(second_raw->get_ref_count(), 2);

    first.reset();
    EXPECT_FALSE(first);
    EXPECT_EQ(second_raw->get_ref_count(), 1);
    EXPECT_EQ(destroyed, 1);
}

/**
 * @brief Verifies that leak hands the reference to the caller and a new owner can share it.
 *
 * Leaks a smart pointer, adopts the raw pointer into a new smart pointer, and releases the
 * caller's reference last so the object is destroyed once.
 */
TEST(Unit_SushiPtrTest, LeakRoundTripHandsReferenceBack)
{
    int destroyed = 0;
    auto owner = make_sushi<Probe>(destroyed);

    Probe* raw = owner.leak();
    EXPECT_FALSE(owner);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->get_ref_count(), 1) << "leak() must not change the count.";

    {
        sushi_ptr<Probe> reacquired(raw);
        EXPECT_EQ(raw->get_ref_count(), 2);
    }
    EXPECT_EQ(raw->get_ref_count(), 1);
    EXPECT_EQ(destroyed, 0);

    raw->release();
    EXPECT_EQ(destroyed, 1);
}
