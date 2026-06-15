/**************************************************************************/
/* test_ref_counted.cpp                                                  */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Unit coverage for the intrusive refcount (RefCounted) and its smart pointer
// (sushi_ptr): single ownership, copy/move semantics, leak(), polymorphic
// deletion, and race-free concurrent counting.

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
    // RefCounted payload that bumps an external counter on destruction.
    struct Tracked : public Core::RefCounted
    {
        std::atomic<int>& destroyed;
        explicit Tracked(std::atomic<int>& d) : destroyed(d) {}
        ~Tracked() override { destroyed.fetch_add(1, std::memory_order_relaxed); }
    };

    // Base/Derived pair to verify polymorphic, single deletion.
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

// make_sushi() yields a single owner; dropping it destroys the object once.
TEST(Unit_RefCountedTest, MakeSushiOwnsAndReleasesOnce)
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

// Copy increments the shared count; move transfers ownership without changing it.
TEST(Unit_RefCountedTest, CopyBumpsMoveTransfers)
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

// leak() returns the raw pointer and empties the smart pointer without touching
// the count, so the caller owns the outstanding reference.
TEST(Unit_RefCountedTest, LeakDetachesWithoutChangingCount)
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

// sushi_ptr<Derived> converts to sushi_ptr<Base>, dispatches virtually, and
// deletes once through the base.
TEST(Unit_RefCountedTest, DerivedConvertsToBaseAndDeletesOnce)
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

// Many threads copying and dropping the same pointer must leave the count
// consistent with no premature destruction — the hot-path guarantee.
TEST(Unit_RefCountedTest, ConcurrentRefCountingIsAtomic)
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
                if (copy->get_ref_count() < 1)   // force the read; not optimizable away
                    std::abort();
            }                                    // atomic release
        });
    }
    for (auto& w : workers)
        w.join();

    EXPECT_EQ(destroyed.load(), 0) << "Object must survive while the root reference is held.";
    EXPECT_EQ(root->get_ref_count(), 1) << "All transient copies must have released exactly once.";

    root.reset();
    EXPECT_EQ(destroyed.load(), 1);
}
