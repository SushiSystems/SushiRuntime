/**************************************************************************/
/* test_in_memory_feed_attachment.cpp                                     */
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
 * @file test_in_memory_feed_attachment.cpp
 * @brief Tests InMemoryFeedAttachment: calls are kept in order, and attach() gives its refusal.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/in_memory_feed_attachment.hpp>

using SushiRuntime::Internal::Profiling::InMemoryFeedAttachment;

/** @brief Tests that attach and detach calls are recorded in the order they are made. */
TEST(Small_InMemoryFeedAttachment, RecordsEachCallInArrivalOrder)
{
    InMemoryFeedAttachment attachment;

    static_cast<void>(attachment.attach());
    attachment.detach();
    static_cast<void>(attachment.attach());

    const std::vector<InMemoryFeedAttachment::Call> expected{
        InMemoryFeedAttachment::Call::Attach,
        InMemoryFeedAttachment::Call::Detach,
        InMemoryFeedAttachment::Call::Attach};
    EXPECT_EQ(attachment.calls(), expected);
}

/** @brief Tests that attach() accepts with an empty refusal when none is set. */
TEST(Small_InMemoryFeedAttachment, AttachAcceptsWhenNoRefusalIsSet)
{
    InMemoryFeedAttachment attachment;

    EXPECT_TRUE(attachment.attach().empty());
}

/** @brief Tests that attach() returns the refusal set, on every call. */
TEST(Small_InMemoryFeedAttachment, AttachReturnsTheRefusalItWasSet)
{
    InMemoryFeedAttachment attachment;
    attachment.refuse_with("no feed for this runtime");

    EXPECT_EQ(attachment.attach(), "no feed for this runtime");
    EXPECT_EQ(attachment.attach(), "no feed for this runtime");
}
