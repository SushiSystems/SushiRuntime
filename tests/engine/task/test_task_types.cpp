/**************************************************************************/
/* test_task_types.cpp                                                    */
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
 * @file test_task_types.cpp
 * @brief Tests the operation hash of OperationID and the parameter slots of TaskMetadata.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/task/task_types.hpp>

namespace
{
    using SushiRuntime::OperationID;
    using SushiRuntime::Internal::Graph::TaskMetadata;
    using SushiRuntime::Literals::operator""_op;

    /**
     * @brief Computes FNV-1a over the bytes of @p text, each byte read as unsigned.
     * @param text Bytes to hash.
     * @param length Number of bytes in @p text.
     * @return The 32-bit hash.
     */
    constexpr std::uint32_t reference_fnv1a(const char* text, std::size_t length)
    {
        std::uint32_t hash = 2166136261u;
        for (std::size_t i = 0; i < length; ++i)
        {
            hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(text[i]));
            hash *= 16777619u;
        }
        return hash;
    }
} // namespace

// Both names are ASCII, so the unsigned byte rule leaves their values unchanged.
static_assert(OperationID::hash("vec.add", 7) == 0xE8523F28u);
static_assert(OperationID::hash("demo.vector_add", 15) == 0x2F4B283Bu);
static_assert("vec.add"_op == OperationID(0xE8523F28u));

// U+00E9 in UTF-8 is 0xC3 0xA9; each byte is read as unsigned on every platform.
static_assert(OperationID::hash("caf\xC3\xA9", 5) == 0xA82B5049u);
static_assert(OperationID::hash("caf\xC3\xA9", 5) == reference_fnv1a("caf\xC3\xA9", 5));

TEST(Small_TaskTypes, AParameterSlotReturnsTheValueStoredInIt)
{
    TaskMetadata metadata;
    metadata.set_param(0, std::uint64_t{0x0123456789ABCDEFu});
    metadata.set_param(static_cast<int>(TaskMetadata::K_MAX_PARAMS) - 1, 2.5);

    EXPECT_EQ(metadata.param<std::uint64_t>(0), 0x0123456789ABCDEFu);
    EXPECT_DOUBLE_EQ(metadata.param<double>(static_cast<int>(TaskMetadata::K_MAX_PARAMS) - 1), 2.5);
}
