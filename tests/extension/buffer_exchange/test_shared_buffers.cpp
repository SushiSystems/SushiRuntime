/**************************************************************************/
/* test_shared_buffers.cpp                                               */
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
 * @file test_shared_buffers.cpp
 * @brief Tests SharedBuffers: shared handles, versions, bounded reads and writes and release.
 * @see tests/extension/buffer_exchange/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/buffer_exchange/shared_buffers.hpp>

#include "support/made_up_handle.hpp"
#include "support/shared_buffer_rig.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names a run of bytes. */
    using Bytes = std::vector<std::byte>;

    /** @brief Holds the size of the shared buffer most tests in this file register. */
    constexpr std::size_t K_BUFFER_BYTES = 64;

    /** @brief Holds the byte a destination is filled with to show what a copy left alone. */
    constexpr std::byte K_GUARD{0xEE};

    /** @brief Holds the length of the chunk the read and write tests move. */
    constexpr std::size_t K_CHUNK = 16;

    /** @brief Returns size bytes that differ by position and by seed. */
    Bytes pattern(std::size_t size, unsigned seed)
    {
        Bytes bytes(size);
        for (std::size_t index = 0; index < size; ++index)
            bytes[index] = static_cast<std::byte>((index * 37u + seed) & 0xFFu);
        return bytes;
    }

    /** @brief Returns whether every byte in the range equals the guard. */
    bool is_all_guard(Bytes::const_iterator first, Bytes::const_iterator last)
    {
        return std::all_of(first, last, [](std::byte value) { return value == K_GUARD; });
    }
} // namespace

TEST(Small_SharedBuffersTest, SharingAHandleAgainKeepsItsHandle)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 1);
    TestSupport::SharedBufferRig rig;

    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    const BufferHandle again = rig.share(buffer.data(), buffer.size());

    EXPECT_EQ(again.id, handle.id);
    EXPECT_EQ(again.version, handle.version);
}

TEST(Small_SharedBuffersTest, DistinctHandlesGetDistinctIDs)
{
    Bytes first = pattern(K_BUFFER_BYTES, 1);
    Bytes second = pattern(K_BUFFER_BYTES / 2, 2);
    TestSupport::SharedBufferRig rig;

    const BufferHandle first_handle = rig.share(first.data(), first.size());
    const BufferHandle second_handle = rig.share(second.data(), second.size());

    EXPECT_NE(second_handle.id, first_handle.id);
    EXPECT_EQ(second_handle.size_bytes, second.size());
}

TEST(Small_SharedBuffersTest, BumpRaisesTheVersionOfALiveHandleAndLeavesAGoneOne)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 3);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    BufferHandle gone;
    gone.id = TestSupport::made_up_handle(999);
    gone.version = 5;
    std::vector<BufferHandle> handles = {handle, gone};

    rig.buffers().bump({handles.data(), handles.size()});

    EXPECT_EQ(handles[0].version, handle.version + 1);
    EXPECT_EQ(handles[1].version, 5u);
}

TEST(Small_SharedBuffersTest, BumpHandlesRaisesTheVersionOfASharedHandle)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 3);
    int not_shared = 0;
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    const GlobalHandle handles[] = {rig.handle_of(buffer.data()), rig.handle_of(&not_shared)};

    rig.buffers().bump_handles({handles, 2});

    const std::optional<BufferHandle> current = rig.lookup(buffer.data());
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->version, handle.version + 1);
    EXPECT_EQ(current->id, handle.id);
}

TEST(Small_SharedBuffersTest, ResolveMapsHandlesToWireHandlesInTheirOrder)
{
    Bytes first = pattern(K_BUFFER_BYTES, 4);
    Bytes second = pattern(K_BUFFER_BYTES, 5);
    TestSupport::SharedBufferRig rig;
    const BufferHandle first_handle = rig.share(first.data(), first.size());
    const BufferHandle second_handle = rig.share(second.data(), second.size());
    const GlobalHandle shared[] = {rig.handle_of(second.data()), rig.handle_of(first.data())};
    std::vector<BufferHandle> resolved;

    ASSERT_TRUE(rig.buffers().resolve({shared, 2}, resolved));

    ASSERT_EQ(resolved.size(), 2u);
    EXPECT_EQ(resolved[0].id, second_handle.id);
    EXPECT_EQ(resolved[1].id, first_handle.id);
}

TEST(Small_SharedBuffersTest, ResolveRefusesAHandleThatIsNotShared)
{
    Bytes first = pattern(K_BUFFER_BYTES, 4);
    int not_shared = 0;
    TestSupport::SharedBufferRig rig;
    rig.share(first.data(), first.size());
    const GlobalHandle one_unknown[] = {rig.handle_of(first.data()), rig.handle_of(&not_shared)};
    std::vector<BufferHandle> resolved;

    EXPECT_FALSE(rig.buffers().resolve({one_unknown, 2}, resolved));
}

TEST(Small_SharedBuffersTest, RefreshRewritesEachHandleToTheCurrentVersion)
{
    Bytes first = pattern(K_BUFFER_BYTES, 4);
    Bytes second = pattern(K_BUFFER_BYTES, 5);
    TestSupport::SharedBufferRig rig;
    const BufferHandle first_handle = rig.share(first.data(), first.size());
    const BufferHandle second_handle = rig.share(second.data(), second.size());
    std::vector<BufferHandle> stale = {first_handle, second_handle};
    rig.bump_address(second.data());

    ASSERT_TRUE(rig.buffers().refresh({stale.data(), stale.size()}));

    EXPECT_EQ(stale[0].version, first_handle.version);
    EXPECT_EQ(stale[1].version, second_handle.version + 1);
}

TEST(Small_SharedBuffersTest, RefreshRefusesAHandleWhoseIDWasUnregistered)
{
    Bytes first = pattern(K_BUFFER_BYTES, 4);
    Bytes second = pattern(K_BUFFER_BYTES, 5);
    TestSupport::SharedBufferRig rig;
    const BufferHandle first_handle = rig.share(first.data(), first.size());
    const BufferHandle second_handle = rig.share(second.data(), second.size());
    std::vector<BufferHandle> handles = {first_handle, second_handle};
    rig.unshare(second.data());

    EXPECT_FALSE(rig.buffers().refresh({handles.data(), handles.size()}));
}

TEST(Small_SharedBuffersTest, ReadCopiesExactlyTheRequestedChunk)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 6);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    Bytes out(32, K_GUARD);

    ASSERT_EQ(rig.buffers().read(handle.id, handle.version, 10, {out.data() + 8, K_CHUNK}),
              BufferAccessStatus::Ok);

    EXPECT_TRUE(std::equal(out.begin() + 8, out.begin() + 24, buffer.begin() + 10));
    EXPECT_TRUE(is_all_guard(out.begin(), out.begin() + 8));
    EXPECT_TRUE(is_all_guard(out.begin() + 24, out.end()));
}

TEST(Small_SharedBuffersTest, ReadEndingOnTheLastByteSucceeds)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 6);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    Bytes tail(K_CHUNK, K_GUARD);

    EXPECT_EQ(rig.buffers().read(handle.id, handle.version, K_BUFFER_BYTES - K_CHUNK,
                            {tail.data(), K_CHUNK}),
              BufferAccessStatus::Ok);

    EXPECT_TRUE(std::equal(tail.begin(), tail.end(), buffer.end() - K_CHUNK));
}

TEST(Small_SharedBuffersTest, ReadOfNothingAtTheEndOnlyChecks)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 6);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    Bytes out(K_CHUNK, K_GUARD);
    const SushiRuntime::span<std::byte> nothing{out.data(), std::size_t{0}};

    EXPECT_EQ(rig.buffers().read(handle.id, handle.version, K_BUFFER_BYTES, nothing),
              BufferAccessStatus::Ok);
}

namespace
{
    /** @brief Holds one read the set must refuse. */
    struct RefusedReadRow
    {
        const char* meaning;         /**< What makes the read unservable. */
        std::uint64_t offset;        /**< Offset the read starts at. */
        std::uint32_t version_added; /**< Added to the buffer's current version. */
        std::uint64_t id;            /**< Buffer id read; 0 names the registered buffer. */
        BufferAccessStatus expected; /**< Status the read must return. */
    };

    /** @brief Names a read row by its meaning in a test report. */
    std::string name_of_read(const ::testing::TestParamInfo<RefusedReadRow>& info)
    {
        return info.param.meaning;
    }

    const RefusedReadRow K_REFUSED_READS[] = {
        {"RunsOneByteBeyondTheEnd", K_BUFFER_BYTES - K_CHUNK + 1, 0, 0,
         BufferAccessStatus::OutOfRange},
        {"StartsBeyondTheSize", K_BUFFER_BYTES + 1, 0, 0, BufferAccessStatus::OutOfRange},
        {"StartsAtTheLargestOffset", std::numeric_limits<std::uint64_t>::max(), 0, 0,
         BufferAccessStatus::OutOfRange},
        {"NamesAnotherVersion", 0, 1, 0, BufferAccessStatus::Stale},
        {"NamesAnUnknownID", 0, 0, 999, BufferAccessStatus::Gone},
    };
} // namespace

class Small_SharedBuffersRefusedRead : public ::testing::TestWithParam<RefusedReadRow>
{
};

TEST_P(Small_SharedBuffersRefusedRead, ReturnsItsStatusAndCopiesNothing)
{
    const RefusedReadRow& row = GetParam();
    Bytes buffer = pattern(K_BUFFER_BYTES, 6);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    const GlobalHandle id = row.id == 0 ? handle.id : TestSupport::made_up_handle(row.id);
    Bytes refused(K_CHUNK, K_GUARD);

    EXPECT_EQ(rig.buffers().read(id, handle.version + row.version_added, row.offset,
                            {refused.data(), K_CHUNK}),
              row.expected);

    EXPECT_TRUE(is_all_guard(refused.begin(), refused.end()));
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_SharedBuffersRefusedRead,
                         ::testing::ValuesIn(K_REFUSED_READS), name_of_read);

namespace
{
    /** @brief Holds one write the set must refuse. */
    struct RefusedWriteRow
    {
        const char* meaning;         /**< What makes the write unservable. */
        std::uint64_t offset;        /**< Offset the write starts at. */
        bool at_dispatched_version;  /**< True writes at the version before the bump. */
        std::uint32_t version_added; /**< Added to the current version otherwise. */
        BufferAccessStatus expected; /**< Status the write must return. */
    };

    /** @brief Names a write row by its meaning in a test report. */
    std::string name_of_write(const ::testing::TestParamInfo<RefusedWriteRow>& info)
    {
        return info.param.meaning;
    }

    const RefusedWriteRow K_REFUSED_WRITES[] = {
        {"AtTheVersionItWasDispatchedWith", 8, true, 0, BufferAccessStatus::Stale},
        {"AtAVersionTheBufferHasNotReached", 8, false, 1, BufferAccessStatus::Stale},
        {"RunningPastTheEnd", K_BUFFER_BYTES - K_CHUNK + 1, false, 0,
         BufferAccessStatus::OutOfRange},
    };
} // namespace

class Small_SharedBuffersRefusedWrite : public ::testing::TestWithParam<RefusedWriteRow>
{
};

TEST_P(Small_SharedBuffersRefusedWrite, ReturnsItsStatusAndWritesNothing)
{
    const RefusedWriteRow& row = GetParam();
    const Bytes original = pattern(K_BUFFER_BYTES, 7);
    const Bytes incoming = pattern(K_CHUNK, 8);
    Bytes buffer = original;
    TestSupport::SharedBufferRig rig;
    BufferHandle handle = rig.share(buffer.data(), buffer.size());
    const std::uint32_t dispatched = handle.version;
    rig.buffers().bump({&handle, 1});
    const std::uint32_t version =
        row.at_dispatched_version ? dispatched : handle.version + row.version_added;

    EXPECT_EQ(rig.buffers().write(handle.id, version, row.offset, {incoming.data(), incoming.size()}),
              row.expected);

    EXPECT_EQ(buffer, original);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_SharedBuffersRefusedWrite,
                         ::testing::ValuesIn(K_REFUSED_WRITES), name_of_write);

TEST(Small_SharedBuffersTest, WriteAtTheCurrentVersionLandsAtItsOffsetOnly)
{
    const Bytes original = pattern(K_BUFFER_BYTES, 7);
    const Bytes incoming = pattern(K_CHUNK, 8);
    Bytes buffer = original;
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());

    ASSERT_EQ(rig.buffers().write(handle.id, handle.version, 8, {incoming.data(), incoming.size()}),
              BufferAccessStatus::Ok);

    EXPECT_TRUE(std::equal(incoming.begin(), incoming.end(), buffer.begin() + 8));
    EXPECT_TRUE(std::equal(buffer.begin(), buffer.begin() + 8, original.begin()));
    EXPECT_TRUE(std::equal(buffer.begin() + 24, buffer.end(), original.begin() + 24));
}

TEST(Small_SharedBuffersTest, WriteLeavesTheVersionAsItWas)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 7);
    const Bytes incoming = pattern(K_CHUNK, 8);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    ASSERT_EQ(rig.buffers().write(handle.id, handle.version, 8, {incoming.data(), incoming.size()}),
              BufferAccessStatus::Ok);

    const std::optional<BufferHandle> current = rig.buffers().lookup(handle.id);

    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->version, handle.version);
}

TEST(Small_SharedBuffersTest, UnshareReturnsTheHandleOfASharedBufferOnce)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 9);
    Bytes never_shared = pattern(K_BUFFER_BYTES, 10);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());

    EXPECT_TRUE(rig.unshare(never_shared.data()).is_null());
    EXPECT_EQ(rig.unshare(buffer.data()), handle.id);
    EXPECT_TRUE(rig.unshare(buffer.data()).is_null());
}

TEST(Small_SharedBuffersTest, AnUnregisteredBufferIsGone)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 9);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    rig.unshare(buffer.data());
    Bytes out(K_CHUNK, K_GUARD);

    EXPECT_FALSE(rig.lookup(buffer.data()).has_value());
    EXPECT_FALSE(rig.buffers().lookup(handle.id).has_value());
    EXPECT_EQ(rig.buffers().read(handle.id, handle.version, 0, {out.data(), out.size()}),
              BufferAccessStatus::Gone);
    EXPECT_EQ(rig.buffers().write(handle.id, handle.version, 0, {out.data(), out.size()}),
              BufferAccessStatus::Gone);
}

TEST(Small_SharedBuffersTest, AHandleSharedAgainAfterUnshareStartsAtVersionOne)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 9);
    TestSupport::SharedBufferRig rig;
    BufferHandle handle = rig.share(buffer.data(), buffer.size());
    rig.buffers().bump({&handle, 1});
    rig.unshare(buffer.data());

    const BufferHandle reshared = rig.share(buffer.data(), buffer.size());

    EXPECT_EQ(reshared.id, handle.id);
    EXPECT_EQ(reshared.version, 1u);
}

TEST(Small_SharedBuffersTest, TheNullHandleIsNeverShared)
{
    TestSupport::SharedBufferRig rig;

    const BufferHandle shared = rig.buffers().share(GlobalHandle::null(), K_BUFFER_BYTES);

    EXPECT_TRUE(shared.id.is_null());
    EXPECT_FALSE(rig.buffers().lookup(GlobalHandle::null()).has_value());
}

TEST(Small_SharedBuffersTest, ABufferWhoseInstanceIsGoneIsGoneToAReader)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 9);
    TestSupport::SharedBufferRig rig;
    const BufferHandle handle = rig.share(buffer.data(), buffer.size());
    ASSERT_EQ(rig.table().retire(buffer.data()), HandleStatus::Ok);
    Bytes out(K_CHUNK, K_GUARD);

    EXPECT_EQ(rig.buffers().read(handle.id, handle.version, 0, {out.data(), out.size()}),
              BufferAccessStatus::Gone);
    EXPECT_TRUE(is_all_guard(out.begin(), out.end()));
}
