/**************************************************************************/
/* test_ref_counted.cpp                                                   */
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
 * @file test_ref_counted.cpp
 * @brief Tests the intrusive RefCounted count and the sushi_ptr that manages it.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

using namespace SushiRuntime;

namespace
{
    /**
     * @brief A test payload for RefCounted that tracks its own destruction.
     *
     * Increments an external atomic counter when its destructor is invoked to verify lifecycle management.
     */
    struct Tracked : public RefCounted
    {
        std::atomic<int>& destroyed;
        explicit Tracked(std::atomic<int>& d) : destroyed(d) {}
        ~Tracked() override { destroyed.fetch_add(1, std::memory_order_relaxed); }
    };

    /**
     * @brief A base class to verify polymorphic destruction in RefCounted objects.
     *
     * Used in conjunction with Derived to ensure that destruction through a base pointer deletes the object correctly exactly once.
     */
    struct Base : public RefCounted
    {
        std::atomic<int>& destroyed;
        explicit Base(std::atomic<int>& d) : destroyed(d) {}
        virtual int tag() const { return 1; }
        ~Base() override = default;
    };

    /**
     * @brief A derived class to verify polymorphic destruction in RefCounted objects.
     *
     * Overrides the base class to increment a destruction counter upon being deleted.
     */
    struct Derived : public Base
    {
        explicit Derived(std::atomic<int>& d) : Base(d) {}
        int tag() const override { return 2; }
        ~Derived() override { destroyed.fetch_add(1, std::memory_order_relaxed); }
    };
}

/**
 * @brief Verifies that make_sushi yields a single owner that destroys the object exactly once.
 *
 * Creates a smart pointer and ensures that the tracked object is destroyed upon the pointer going out of scope.
 */
TEST(Medium_RefCountedTest, MakeSushiOwnsAndReleasesOnce)
{
    std::atomic<int> destroyed{0};
    {
        auto p = make_sushi<Tracked>(destroyed);
        ASSERT_TRUE(p);
        EXPECT_EQ(p->ref_count(), 1);
        EXPECT_EQ(destroyed.load(), 0);
    }
    EXPECT_EQ(destroyed.load(), 1) << "Object must be destroyed exactly once when its sole owner drops.";
}

/**
 * @brief Verifies that copying increments the refcount while moving transfers ownership without
 *        altering it.
 *
 * Tracks the reference count through move and copy operations and ensures proper decrementing upon scope exit.
 */
TEST(Medium_RefCountedTest, CopyBumpsMoveTransfers)
{
    std::atomic<int> destroyed{0};
    auto a = make_sushi<Tracked>(destroyed);
    EXPECT_EQ(a->ref_count(), 1);

    sushi_ptr<Tracked> b(std::move(a));          // move: no refcount change
    EXPECT_EQ(a.get(), nullptr);
    EXPECT_FALSE(a);
    EXPECT_EQ(b->ref_count(), 1);

    {
        sushi_ptr<Tracked> c = b;                // copy: +1
        EXPECT_EQ(b->ref_count(), 2);
        EXPECT_TRUE(c == b);
    }
    EXPECT_EQ(b->ref_count(), 1) << "Copy must release its reference on scope exit.";
    EXPECT_EQ(destroyed.load(), 0);
}

/**
 * @brief Verifies that leak() detaches the raw pointer without modifying the reference count.
 *
 * Asserts that the smart pointer is emptied while the object's reference count remains unchanged, leaving ownership to the caller.
 */
TEST(Medium_RefCountedTest, LeakDetachesWithoutChangingCount)
{
    std::atomic<int> destroyed{0};
    auto p = make_sushi<Tracked>(destroyed);

    Tracked* raw = p.leak();
    EXPECT_EQ(p.get(), nullptr) << "leak() must empty the smart pointer.";
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->ref_count(), 1) << "leak() must not touch the refcount.";
    EXPECT_EQ(destroyed.load(), 0);

    raw->release();                              // caller-owned reference
    EXPECT_EQ(destroyed.load(), 1);
}

/**
 * @brief Verifies polymorphic conversion and single deletion through a base pointer.
 *
 * Converts a derived smart pointer to a base smart pointer, asserts virtual dispatch works, and ensures exactly one destruction occurs.
 */
TEST(Medium_RefCountedTest, DerivedConvertsToBaseAndDeletesOnce)
{
    std::atomic<int> destroyed{0};
    {
        sushi_ptr<Base> base = make_sushi<Derived>(destroyed); // converting ctor
        ASSERT_TRUE(base);
        EXPECT_EQ(base->tag(), 2) << "Virtual dispatch must reach the derived override.";
        EXPECT_EQ(base->ref_count(), 1);
    }
    EXPECT_EQ(destroyed.load(), 1) << "Deleting via base must run the derived destructor exactly once.";
}

/**
 * @brief Verifies that concurrent reference counting operations are thread-safe and atomic.
 *
 * Spawns multiple threads to aggressively copy and drop a shared pointer, asserting that the object survives and count remains consistent.
 */
TEST(Medium_RefCountedTest, ConcurrentRefCountingIsAtomic)
{
    std::atomic<int> destroyed{0};
    auto root = make_sushi<Tracked>(destroyed);

    constexpr int THREADS = 8;
    constexpr int ITERS   = 20000;

    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
    {
        workers.emplace_back([&root]()
        {
            for (int i = 0; i < ITERS; ++i)
            {
                sushi_ptr<Tracked> copy = root;  // atomic add_ref
                if (copy->ref_count() < 1)   // force the read; not optimizable away
                    std::abort();
            }                                    // atomic release
        });
    }
    for (auto& w : workers)
        w.join();

    EXPECT_EQ(destroyed.load(), 0) << "Object must survive while the root reference is held.";
    EXPECT_EQ(root->ref_count(), 1) << "All transient copies must have released exactly once.";

    root.reset();
    EXPECT_EQ(destroyed.load(), 1);
}
