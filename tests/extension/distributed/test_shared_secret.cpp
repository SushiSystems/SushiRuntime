/**************************************************************************/
/* test_shared_secret.cpp                                                 */
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
 * @file test_shared_secret.cpp
 * @brief Tests SharedSecret: reading from a file, the environment and memory.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>


using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a vector's bytes as a span. */
    span<const std::uint8_t> bytes_of(const std::vector<std::uint8_t>& bytes)
    {
        return { bytes.data(), bytes.size() };
    }

    /** @brief Returns the bytes first, first + 1, ... of the given count. */
    std::vector<std::uint8_t> counting(std::uint8_t first, std::size_t count)
    {
        std::vector<std::uint8_t> bytes(count);
        for (std::size_t index = 0; index < count; ++index)
            bytes[index] = static_cast<std::uint8_t>(first + index);
        return bytes;
    }

    /** @brief Returns the error of a refused secret, or nothing when one was built. */
    std::optional<Error> error_of(const Result<SharedSecret>& secret)
    {
        if (secret.has_value())
            return std::nullopt;
        return secret.error();
    }

    /** @brief Writes a file in the working directory and removes it when destroyed. */
    class ScratchFile final
    {
        public:
            /** @brief Writes @p bytes to a file called @p name in the working directory. */
            ScratchFile(const std::string& name, const std::vector<std::uint8_t>& bytes)
                : path_(std::filesystem::current_path() / name)
            {
                std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
                stream.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            }

            /** @brief Removes the file. */
            ~ScratchFile()
            {
                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }

            ScratchFile(const ScratchFile&) = delete;
            ScratchFile& operator=(const ScratchFile&) = delete;
            ScratchFile(ScratchFile&&) = delete;
            ScratchFile& operator=(ScratchFile&&) = delete;

            /** @brief Returns where the file is. */
            const std::filesystem::path& path() const noexcept
            {
                return path_;
            }

        private:
            /** @brief Holds the absolute path of the file. */
            std::filesystem::path path_;
    };
}

/** @brief Tests that a file one byte under the minimum is refused, by name and length. */
TEST(Medium_SharedSecretTest, FromFileRefusesAShortFile)
{
    const ScratchFile file("sr_test_shared_secret_short.bin", counting(0x00, 31));

    const std::optional<Error> error =
        error_of(SharedSecret::from_file(file.path()));

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, ErrorCode::InvalidAccess);
    EXPECT_NE(error->message.find("sr_test_shared_secret_short.bin"), std::string::npos);
    EXPECT_NE(error->message.find("31 bytes"), std::string::npos);
}

/** @brief Tests that a file of exactly the minimum length is read byte for byte. */
TEST(Medium_SharedSecretTest, FromFileAcceptsThirtyTwoBytes)
{
    const std::vector<std::uint8_t> content = counting(0xe0, 32);
    const ScratchFile file("sr_test_shared_secret_exact.bin", content);

    const SharedSecret secret = SharedSecret::from_file(file.path()).value();

    EXPECT_FALSE(secret.empty());
    ASSERT_EQ(secret.size(), content.size());
    EXPECT_EQ(std::vector<std::uint8_t>(secret.bytes().begin(), secret.bytes().end()), content);
}

/** @brief Tests that a missing file is refused with its path in the message. */
TEST(Medium_SharedSecretTest, FromFileMissingNamesThePath)
{
    const std::filesystem::path missing =
        std::filesystem::current_path() / "sr_test_shared_secret_missing.bin";

    const std::optional<Error> error =
        error_of(SharedSecret::from_file(missing));

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, ErrorCode::InvalidAccess);
    EXPECT_NE(error->message.find(missing.generic_string()), std::string::npos);
}

/** @brief Tests that an unset environment variable yields the empty secret. */
TEST(Medium_SharedSecretTest, FromEnvironmentUnsetIsEmpty)
{
    const SharedSecret secret =
        SharedSecret::from_environment("SR_TEST_SHARED_SECRET_VARIABLE_NOBODY_SETS").value();

    EXPECT_TRUE(secret.empty());
    EXPECT_EQ(secret.size(), 0u);
}

/** @brief Tests that a move empties its source and a clone leaves it whole. */
TEST(Medium_SharedSecretTest, MoveEmptiesTheSourceAndCloneKeepsIt)
{
    const std::vector<std::uint8_t> content = counting(0x40, 48);
    SharedSecret original = SharedSecret::from_bytes(bytes_of(content)).value();

    const SharedSecret copy = original.clone();
    EXPECT_EQ(original.size(), content.size());
    EXPECT_EQ(std::vector<std::uint8_t>(copy.bytes().begin(), copy.bytes().end()), content);

    const SharedSecret moved = std::move(original);
    EXPECT_EQ(moved.size(), content.size());
    EXPECT_TRUE(original.empty());
    EXPECT_TRUE(SharedSecret().empty());
}

/** @brief Tests that bytes passed in memory meet the same minimum as a file. */
TEST(Medium_SharedSecretTest, FromBytesRefusesAShortSecret)
{
    const std::vector<std::uint8_t> content = counting(0x00, 31);

    const std::optional<Error> error =
        error_of(SharedSecret::from_bytes(bytes_of(content)));

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, ErrorCode::InvalidAccess);
    EXPECT_NE(error->message.find("31 bytes"), std::string::npos);
}
