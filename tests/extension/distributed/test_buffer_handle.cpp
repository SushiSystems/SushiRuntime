/**************************************************************************/
/* test_buffer_handle.cpp                                                 */
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
 * @file test_buffer_handle.cpp
 * @brief Tests the registry that versions the buffers a cluster shares.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "buffer_registry.hpp"

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

    /** @brief Returns size bytes that differ by position and by seed. */
    Bytes pattern(std::size_t size, unsigned seed)
    {
        Bytes bytes(size);
        for (std::size_t index = 0; index < size; ++index)
            bytes[index] = static_cast<std::byte>((index * 37u + seed) & 0xFFu);
        return bytes;
    }
} // namespace

TEST(Unit_BufferRegistryTest, RegisteringASharedAddressKeepsItsHandle)
{
    Bytes first = pattern(K_BUFFER_BYTES, 1);
    Bytes second = pattern(K_BUFFER_BYTES / 2, 2);
    BufferRegistry registry;

    const BufferHandle handle = registry.register_buffer(first.data(), first.size());
    EXPECT_EQ(handle.id.value, 1u);
    EXPECT_EQ(handle.version, 1u);
    EXPECT_EQ(handle.size_bytes, first.size());

    const BufferHandle again = registry.register_buffer(first.data(), first.size());
    EXPECT_EQ(again.id, handle.id);
    EXPECT_EQ(again.version, handle.version);

    const BufferHandle other = registry.register_buffer(second.data(), second.size());
    EXPECT_EQ(other.id.value, 2u);
    EXPECT_EQ(other.size_bytes, second.size());
}

TEST(Unit_BufferRegistryTest, BumpRaisesTheVersionAndSkipsAGoneID)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 3);
    BufferRegistry registry;
    const BufferHandle handle = registry.register_buffer(buffer.data(), buffer.size());

    BufferHandle gone;
    gone.id.value = 999;
    gone.version = 5;
    std::vector<BufferHandle> handles = {handle, gone};
    registry.bump({handles.data(), handles.size()});
    EXPECT_EQ(handles[0].version, handle.version + 1);
    EXPECT_EQ(handles[1].version, 5u);

    void* const addresses[] = {buffer.data(), &gone};
    registry.bump_addresses({addresses, 2});

    const std::optional<BufferHandle> current = registry.lookup(buffer.data());
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->version, handle.version + 2);
    EXPECT_EQ(current->id, handle.id);
}

TEST(Unit_BufferRegistryTest, ResolveAndRefreshFollowTheCurrentVersion)
{
    Bytes first = pattern(K_BUFFER_BYTES, 4);
    Bytes second = pattern(K_BUFFER_BYTES, 5);
    BufferRegistry registry;
    const BufferHandle first_handle = registry.register_buffer(first.data(), first.size());
    const BufferHandle second_handle = registry.register_buffer(second.data(), second.size());

    void* const shared[] = {second.data(), first.data()};
    std::vector<BufferHandle> resolved;
    ASSERT_TRUE(registry.resolve({shared, 2}, resolved));
    ASSERT_EQ(resolved.size(), 2u);
    EXPECT_EQ(resolved[0].id, second_handle.id);
    EXPECT_EQ(resolved[1].id, first_handle.id);

    void* const one_unknown[] = {first.data(), &registry};
    EXPECT_FALSE(registry.resolve({one_unknown, 2}, resolved));

    std::vector<BufferHandle> stale = {first_handle, second_handle};
    void* const bumped[] = {second.data()};
    registry.bump_addresses({bumped, 1});
    ASSERT_TRUE(registry.refresh({stale.data(), stale.size()}));
    EXPECT_EQ(stale[0].version, first_handle.version);
    EXPECT_EQ(stale[1].version, second_handle.version + 1);

    EXPECT_EQ(registry.unregister_buffer(second.data()), second_handle.id);
    EXPECT_FALSE(registry.refresh({stale.data(), stale.size()}));
}

TEST(Unit_BufferRegistryTest, ReadAtAnOffsetCopiesExactlyTheChunk)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 6);
    BufferRegistry registry;
    const BufferHandle handle = registry.register_buffer(buffer.data(), buffer.size());

    Bytes out(32, K_GUARD);
    ASSERT_EQ(registry.read(handle.id, handle.version, 10, {out.data() + 8, 16}),
              BufferAccessStatus::Ok);
    EXPECT_TRUE(std::equal(out.begin() + 8, out.begin() + 24, buffer.begin() + 10));
    EXPECT_TRUE(std::all_of(out.begin(), out.begin() + 8,
                            [](std::byte value) { return value == K_GUARD; }));
    EXPECT_TRUE(std::all_of(out.begin() + 24, out.end(),
                            [](std::byte value) { return value == K_GUARD; }));

    Bytes tail(16, K_GUARD);
    EXPECT_EQ(registry.read(handle.id, handle.version, K_BUFFER_BYTES - 16, {tail.data(), 16}),
              BufferAccessStatus::Ok);
    EXPECT_TRUE(std::equal(tail.begin(), tail.end(), buffer.end() - 16));

    const Bytes guards(16, K_GUARD);
    Bytes refused = guards;
    EXPECT_EQ(registry.read(handle.id, handle.version, K_BUFFER_BYTES - 15, {refused.data(), 16}),
              BufferAccessStatus::OutOfRange);
    const SushiRuntime::span<std::byte> nothing{refused.data(), std::size_t{0}};
    EXPECT_EQ(registry.read(handle.id, handle.version, K_BUFFER_BYTES + 1, nothing),
              BufferAccessStatus::OutOfRange);
    EXPECT_EQ(registry.read(handle.id, handle.version,
                            std::numeric_limits<std::uint64_t>::max(), {refused.data(), 16}),
              BufferAccessStatus::OutOfRange);
    EXPECT_EQ(registry.read(handle.id, handle.version + 1, 0, {refused.data(), 16}),
              BufferAccessStatus::Stale);
    EXPECT_EQ(registry.read(GlobalBufferID{999}, handle.version, 0, {refused.data(), 16}),
              BufferAccessStatus::Gone);
    EXPECT_EQ(refused, guards);

    EXPECT_EQ(registry.read(handle.id, handle.version, K_BUFFER_BYTES, nothing),
              BufferAccessStatus::Ok);
}

TEST(Unit_BufferRegistryTest, WriteAtAnOldVersionIsRefused)
{
    const Bytes original = pattern(K_BUFFER_BYTES, 7);
    const Bytes incoming = pattern(16, 8);
    Bytes buffer = original;
    BufferRegistry registry;
    BufferHandle handle = registry.register_buffer(buffer.data(), buffer.size());
    const std::uint32_t dispatched = handle.version;

    registry.bump({&handle, 1});
    ASSERT_EQ(handle.version, dispatched + 1);

    EXPECT_EQ(registry.write(handle.id, dispatched, 8, {incoming.data(), incoming.size()}),
              BufferAccessStatus::Stale);
    EXPECT_EQ(registry.write(handle.id, handle.version + 1, 8, {incoming.data(), incoming.size()}),
              BufferAccessStatus::Stale);
    EXPECT_EQ(registry.write(handle.id, handle.version, K_BUFFER_BYTES - 15,
                             {incoming.data(), incoming.size()}),
              BufferAccessStatus::OutOfRange);
    EXPECT_EQ(buffer, original);

    ASSERT_EQ(registry.write(handle.id, handle.version, 8, {incoming.data(), incoming.size()}),
              BufferAccessStatus::Ok);
    EXPECT_TRUE(std::equal(incoming.begin(), incoming.end(), buffer.begin() + 8));
    EXPECT_TRUE(std::equal(buffer.begin(), buffer.begin() + 8, original.begin()));
    EXPECT_TRUE(std::equal(buffer.begin() + 24, buffer.end(), original.begin() + 24));

    const std::optional<BufferHandle> current = registry.lookup_by_id(handle.id);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->version, handle.version);
}

TEST(Unit_BufferRegistryTest, UnregisterReturnsTheID)
{
    Bytes buffer = pattern(K_BUFFER_BYTES, 9);
    Bytes never_shared = pattern(K_BUFFER_BYTES, 10);
    BufferRegistry registry;
    const BufferHandle handle = registry.register_buffer(buffer.data(), buffer.size());

    EXPECT_FALSE(registry.unregister_buffer(never_shared.data()).valid());
    EXPECT_EQ(registry.unregister_buffer(buffer.data()), handle.id);
    EXPECT_FALSE(registry.unregister_buffer(buffer.data()).valid());

    EXPECT_FALSE(registry.lookup(buffer.data()).has_value());
    EXPECT_FALSE(registry.lookup_by_id(handle.id).has_value());

    Bytes out(16, K_GUARD);
    EXPECT_EQ(registry.read(handle.id, handle.version, 0, {out.data(), out.size()}),
              BufferAccessStatus::Gone);
    EXPECT_EQ(registry.write(handle.id, handle.version, 0, {out.data(), out.size()}),
              BufferAccessStatus::Gone);

    // An address shared again after a release is a new buffer: ids are not reused.
    const BufferHandle reshared = registry.register_buffer(buffer.data(), buffer.size());
    EXPECT_NE(reshared.id, handle.id);
    EXPECT_EQ(reshared.version, 1u);
}
