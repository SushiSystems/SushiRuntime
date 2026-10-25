/**************************************************************************/
/* test_context_access.cpp                                                */
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
 * @file test_context_access.cpp
 * @brief Tests the way from a ContextHandle to the engine's context and back.
 * @see tests/surface/context_access/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/context/context_handle_access.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory_exchange/capability_cache.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Keeps a handle the way a Runtime does, in a private context_ member. */
    class HandleOwner final
    {
        public:
            /** @brief Takes the handle to keep. */
            explicit HandleOwner(ContextHandle context) noexcept : context_(std::move(context))
            {
            }

        private:
            friend struct Detail::ContextHandleAccess;

            /** @brief The handle the access reads back. */
            ContextHandle context_;
    };
} // namespace

/** @brief Tests that a handle gives back the context it was built from. */
TEST(Medium_ContextAccess, AHandleReturnsTheContextItWasBuiltFrom)
{
    const std::shared_ptr<Internal::Execution::RuntimeContext> built =
        SushiRuntime::Test::make_context();

    const ContextHandle handle = Internal::ContextAccess::handle(built);

    ASSERT_TRUE(static_cast<bool>(handle));
    EXPECT_EQ(&Internal::ContextAccess::context(handle), built.get());
    EXPECT_EQ(Internal::ContextAccess::shared(handle).get(), built.get());
}

/** @brief Tests that the owner of a handle is read through the handle it keeps. */
TEST(Medium_ContextAccess, AnOwnerIsReadThroughItsHandle)
{
    const std::shared_ptr<Internal::Execution::RuntimeContext> built =
        SushiRuntime::Test::make_context();
    const HandleOwner owner(Internal::ContextAccess::handle(built));

    EXPECT_EQ(&Internal::ContextAccess::context(owner), built.get());
    EXPECT_EQ(Internal::ContextAccess::shared(owner).get(), built.get());
}

/** @brief Tests that no context makes an empty handle, whose share is null. */
TEST(Medium_ContextAccess, ANullContextMakesAnEmptyHandle)
{
    const ContextHandle handle = Internal::ContextAccess::handle(nullptr);

    EXPECT_FALSE(static_cast<bool>(handle));
    EXPECT_TRUE(Internal::ContextAccess::shared(handle) == nullptr);
}

/** @brief Tests that a runtime's handles and their owners reach one capability cache. */
TEST(Medium_ContextAccess, EveryShareOfAHandleReachesOneCapabilityCache)
{
    const ContextHandle handle =
        Internal::ContextAccess::handle(SushiRuntime::Test::make_context());
    const ContextHandle copy = handle;
    const HandleOwner owner(handle);

    Internal::MemoryExchange::CapabilityCache& cache =
        Internal::ContextAccess::capability_cache(handle);

    EXPECT_EQ(&Internal::ContextAccess::capability_cache(copy), &cache);
    EXPECT_EQ(&Internal::ContextAccess::capability_cache(owner), &cache);
}

/** @brief Tests that two runtimes built from one engine context keep two capability caches. */
TEST(Medium_ContextAccess, EachHandleBuiltKeepsItsOwnCapabilityCache)
{
    const std::shared_ptr<Internal::Execution::RuntimeContext> built =
        SushiRuntime::Test::make_context();
    const ContextHandle first = Internal::ContextAccess::handle(built);
    const ContextHandle second = Internal::ContextAccess::handle(built);

    EXPECT_NE(&Internal::ContextAccess::capability_cache(first),
              &Internal::ContextAccess::capability_cache(second));
}
