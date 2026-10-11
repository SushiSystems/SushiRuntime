/**************************************************************************/
/* shared_secret.cpp                                                      */
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
 * @file shared_secret.cpp
 * @brief Implements SharedSecret: reading, owning and wiping the cluster's key.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/shared_secret.hpp>

#include <fstream>
#include <ios>
#include <optional>
#include <utility>

#include <SushiRuntime/core/error.hpp>

#include "environment.hpp"
#include "hmac_sha256.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Throws the exception every refused secret is reported with. */
            [[noreturn]] void refuse(std::string message)
            {
                throw SushiException(
                    Error{ErrorCode::InvalidAccess, std::move(message)});
            }

            /**
             * @brief Refuses a secret whose length is outside the allowed range.
             * @param origin Names where the bytes came from, for the message.
             */
            void require_length(std::uintmax_t size, const std::string& origin)
            {
                if (size < SharedSecret::K_MINIMUM_BYTES)
                    refuse(origin + " holds " + std::to_string(size)
                           + " bytes; a cluster secret needs at least "
                           + std::to_string(SharedSecret::K_MINIMUM_BYTES));

                if (size > SharedSecret::K_MAXIMUM_BYTES)
                    refuse(origin + " holds " + std::to_string(size)
                           + " bytes; a cluster secret holds at most "
                           + std::to_string(SharedSecret::K_MAXIMUM_BYTES));
            }

            /**
             * @brief Returns the whole content of a secret file.
             * @param origin Names the file, and whatever pointed at it, for the messages.
             */
            std::vector<std::uint8_t> read_secret_file(const std::filesystem::path& path,
                                                       const std::string& origin)
            {
                std::ifstream stream(path, std::ios::binary | std::ios::ate);
                if (!stream)
                    refuse("cannot open " + origin);

                const std::streamoff end = stream.tellg();
                if (end < 0)
                    refuse("cannot measure " + origin);

                const std::uintmax_t size = static_cast<std::uintmax_t>(end);
                require_length(size, origin);

                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
                stream.seekg(0);
                stream.read(reinterpret_cast<char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()));
                if (static_cast<std::uintmax_t>(stream.gcount()) != size)
                {
                    wipe_bytes(bytes.data(), bytes.size());
                    refuse("cannot read all " + std::to_string(size) + " bytes of " + origin);
                }

                return bytes;
            }

            /** @brief Returns the text that names a secret file in a message. */
            std::string describe_file(const std::filesystem::path& path)
            {
                return "cluster secret file '" + path.generic_string() + "'";
            }
        } // namespace

        SharedSecret::SharedSecret() noexcept = default;

        SharedSecret::~SharedSecret()
        {
            wipe();
        }

        SharedSecret::SharedSecret(SharedSecret&& other) noexcept
            : bytes_(std::move(other.bytes_))
        {
            other.bytes_.clear();
        }

        SharedSecret& SharedSecret::operator=(SharedSecret&& other) noexcept
        {
            if (this != &other)
            {
                wipe();
                bytes_ = std::move(other.bytes_);
                other.bytes_.clear();
            }

            return *this;
        }

        SharedSecret SharedSecret::from_file(const std::filesystem::path& path)
        {
            return SharedSecret(read_secret_file(path, describe_file(path)));
        }

        SharedSecret SharedSecret::from_environment(const std::string& variable_name)
        {
            const std::optional<std::string> value = read_environment(variable_name.c_str());
            if (!value || value->empty())
                return SharedSecret();

            const std::filesystem::path path(*value);
            return SharedSecret(read_secret_file(
                path, describe_file(path) + " (named by " + variable_name + ")"));
        }

        SharedSecret SharedSecret::from_bytes(span<const std::uint8_t> bytes)
        {
            require_length(bytes.size(), "the cluster secret passed in memory");
            return SharedSecret(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        }

        SharedSecret SharedSecret::clone() const
        {
            return SharedSecret(std::vector<std::uint8_t>(bytes_));
        }

        bool SharedSecret::empty() const noexcept
        {
            return bytes_.empty();
        }

        std::size_t SharedSecret::size() const noexcept
        {
            return bytes_.size();
        }

        span<const std::uint8_t> SharedSecret::bytes() const noexcept
        {
            return span<const std::uint8_t>(bytes_.data(), bytes_.size());
        }

        SharedSecret::SharedSecret(std::vector<std::uint8_t>&& bytes) noexcept
            : bytes_(std::move(bytes))
        {

        }

        void SharedSecret::wipe() noexcept
        {
            wipe_bytes(bytes_.data(), bytes_.size());
            std::vector<std::uint8_t>().swap(bytes_);
        }

    } // namespace Distributed
} // namespace SushiRuntime
