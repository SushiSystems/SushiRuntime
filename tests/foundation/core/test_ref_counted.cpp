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
 * @file test_ref_counted.cpp
 * @brief Tests the intrusive RefCounted count and the sushi_ptr that manages it.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <functional>
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
TEST(Small_RefCountedTest, MakeSushiOwnsAndReleasesOnce)
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
TEST(Small_RefCountedTest, CopyBumpsMoveTransfers)
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
 * @brief Verifies polymorphic conversion and single deletion through a base pointer.
 *
 * Converts a derived smart pointer to a base smart pointer, asserts virtual dispatch works, and ensures exactly one destruction occurs.
 */
TEST(Small_RefCountedTest, DerivedConvertsToBaseAndDeletesOnce)
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

namespace
{
    /**
     * @brief Copies and drops the shared pointer @p iterations times on the calling thread.
     *
     * Each copy adds a reference and each drop releases it, so the count is back where it started.
     */
    void churn(const sushi_ptr<Tracked>& shared, int iterations)
    {
        for (int i = 0; i < iterations; ++i)
        {
            sushi_ptr<Tracked> copy = shared;
            EXPECT_GE(copy->ref_count(), 2);
        }
    }

    /**
     * @brief Starts @p count threads that each run churn on @p shared and joins them all.
     */
    void churn_on_threads(const sushi_ptr<Tracked>& shared, int count, int iterations)
    {
        std::vector<std::thread> workers;
        for (int t = 0; t < count; ++t)
            workers.emplace_back(churn, std::cref(shared), iterations);
        for (std::thread& worker : workers)
            worker.join();
    }
}

/**
 * @brief Verifies that concurrent reference counting operations are thread-safe and atomic.
 *
 * Spawns multiple threads to copy and drop a shared pointer, asserting that the object survives and the count is consistent.
 */
TEST(Medium_RefCountedTest, ConcurrentRefCountingIsAtomic)
{
    std::atomic<int> destroyed{0};
    auto root = make_sushi<Tracked>(destroyed);

    churn_on_threads(root, 8, 20000);

    EXPECT_EQ(destroyed.load(), 0) << "Object must survive while the root reference is held.";
    EXPECT_EQ(root->ref_count(), 1) << "All transient copies must have released exactly once.";

    root.reset();
    EXPECT_EQ(destroyed.load(), 1);
}
