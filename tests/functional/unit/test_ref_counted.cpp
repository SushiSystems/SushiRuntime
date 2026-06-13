/**************************************************************************/
/* test_ref_counted.cpp                                                  */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_ref_counted.cpp
 * @brief Unit coverage for the intrusive refcount (RefCounted) and its smart
 *        pointer (sushi_ptr).
 *
 * These two types sit on the hottest paths in the runtime (every Task / Node is
 * refcounted and passed around as a sushi_ptr), so their ownership contract is
 * asserted directly rather than only exercised in passing:
 *   - make_sushi yields a single owner and the object dies when it drops,
 *   - copy bumps the count, move transfers without touching it,
 *   - leak() detaches ownership without changing the count,
 *   - a derived pointer converts to a base pointer and still deletes once,
 *   - concurrent copy/drop is race-free because the count is atomic.
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
    /** @brief RefCounted payload that bumps an external counter on destruction. */
    struct Tracked : public Core::RefCounted
    {
        std::atomic<int>& destroyed;
        explicit Tracked(std::atomic<int>& d) : destroyed(d) {}
        ~Tracked() override { destroyed.fetch_add(1, std::memory_order_relaxed); }
    };

    /** @brief Base/Derived pair to verify polymorphic, single deletion. */
    struct Base : public Core::RefCounted
    {
        std::atomic<int>& destroyed;
        explicit Base(std::atomic<int>& d) : destroyed(d) {}
        virtual int tag() const { return 1; }
        ~Base() override = default;
    };

    struct Derived : public Base
    {
        explicit Derived(std::atomic<int>& d) : Base(d) {}
        int tag() const override { return 2; }
        ~Derived() override { destroyed.fetch_add(1, std::memory_order_relaxed); }
    };
}

/**
 * @test MakeSushiOwnsAndReleasesOnce
 * @brief make_sushi() produces exactly one owner (count 1); dropping that owner
 * destroys the object exactly once.
 */
TEST(RefCountedTest, MakeSushiOwnsAndReleasesOnce)
{
    std::atomic<int> destroyed{0};
    {
        auto p = make_sushi<Tracked>(destroyed);
        ASSERT_TRUE(p);
        EXPECT_EQ(p->get_ref_count(), 1);
        EXPECT_EQ(destroyed.load(), 0);
    }
    EXPECT_EQ(destroyed.load(), 1) << "Object must be destroyed exactly once when its sole owner drops.";
}

/**
 * @test CopyBumpsMoveTransfers
 * @brief Copy increments the shared count; move transfers ownership and leaves
 * the source empty without changing the count.
 */
TEST(RefCountedTest, CopyBumpsMoveTransfers)
{
    std::atomic<int> destroyed{0};
    auto a = make_sushi<Tracked>(destroyed);
    EXPECT_EQ(a->get_ref_count(), 1);

    sushi_ptr<Tracked> b(std::move(a));          // move: no refcount change
    EXPECT_EQ(a.get(), nullptr);
    EXPECT_FALSE(a);
    EXPECT_EQ(b->get_ref_count(), 1);

    {
        sushi_ptr<Tracked> c = b;                // copy: +1
        EXPECT_EQ(b->get_ref_count(), 2);
        EXPECT_TRUE(c == b);
    }
    EXPECT_EQ(b->get_ref_count(), 1) << "Copy must release its reference on scope exit.";
    EXPECT_EQ(destroyed.load(), 0);
}

/**
 * @test LeakDetachesWithoutChangingCount
 * @brief leak() hands back the raw pointer and nulls the smart pointer WITHOUT
 * altering the refcount, so the caller now owns the outstanding reference and is
 * responsible for one matching release().
 */
TEST(RefCountedTest, LeakDetachesWithoutChangingCount)
{
    std::atomic<int> destroyed{0};
    auto p = make_sushi<Tracked>(destroyed);

    Tracked* raw = p.leak();
    EXPECT_EQ(p.get(), nullptr) << "leak() must empty the smart pointer.";
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->get_ref_count(), 1) << "leak() must not touch the refcount.";
    EXPECT_EQ(destroyed.load(), 0);

    raw->release();                              // caller-owned reference
    EXPECT_EQ(destroyed.load(), 1);
}

/**
 * @test DerivedConvertsToBaseAndDeletesOnce
 * @brief A sushi_ptr<Derived> converts to sushi_ptr<Base>, dispatches virtually,
 * and triggers a single polymorphic destruction through the base.
 */
TEST(RefCountedTest, DerivedConvertsToBaseAndDeletesOnce)
{
    std::atomic<int> destroyed{0};
    {
        sushi_ptr<Base> base = make_sushi<Derived>(destroyed); // converting ctor
        ASSERT_TRUE(base);
        EXPECT_EQ(base->tag(), 2) << "Virtual dispatch must reach the derived override.";
        EXPECT_EQ(base->get_ref_count(), 1);
    }
    EXPECT_EQ(destroyed.load(), 1) << "Deleting via base must run the derived destructor exactly once.";
}

/**
 * @test ConcurrentRefCountingIsAtomic
 * @brief Many threads concurrently copying and dropping the same pointer must
 * leave the shared count consistent (back to the single root reference) with no
 * premature destruction — the guarantee that makes the atomic count usable on
 * the scheduler's hot path.
 */
TEST(RefCountedTest, ConcurrentRefCountingIsAtomic)
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
                // Force the read so the copy cannot be optimized away.
                if (copy->get_ref_count() < 1) std::abort();
            }                                    // atomic release
        });
    }
    for (auto& w : workers) w.join();

    EXPECT_EQ(destroyed.load(), 0) << "Object must survive while the root reference is held.";
    EXPECT_EQ(root->get_ref_count(), 1) << "All transient copies must have released exactly once.";

    root.reset();
    EXPECT_EQ(destroyed.load(), 1);
}
