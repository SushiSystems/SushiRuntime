/**************************************************************************/
/* shared_secret.hpp                                                      */
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
 * @file shared_secret.hpp
 * @brief Declares SharedSecret, the key a master and its workers authenticate with.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/span.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Owns the bytes of a cluster's shared secret and wipes them when it dies.
         *
         * A secret moves and never copies implicitly; clone() is the one way to duplicate it.
         * A default-constructed or moved-from secret is empty.
         */
        class SUSHIRUNTIME_API SharedSecret final
        {
            public:
                /** @brief Counts the fewest bytes a secret that is not empty may hold. */
                static constexpr std::size_t K_MINIMUM_BYTES = 32;

                /** @brief Counts the most bytes a secret may hold. */
                static constexpr std::size_t K_MAXIMUM_BYTES = 1024 * 1024;

                /** @brief Builds the empty secret, which authenticates nothing. */
                SharedSecret() noexcept;

                /** @brief Wipes the bytes. */
                ~SharedSecret();

                SharedSecret(const SharedSecret&) = delete;
                SharedSecret& operator=(const SharedSecret&) = delete;

                /** @brief Takes the bytes of @p other and leaves it empty. */
                SharedSecret(SharedSecret&& other) noexcept;

                /** @brief Wipes the bytes held, then takes those of @p other, leaving it empty. */
                SharedSecret& operator=(SharedSecret&& other) noexcept;

                /**
                 * @brief Reads a secret from a file, byte for byte.
                 * @return The secret, or InvalidAccess, naming the file, when it cannot be
                 *         read or holds fewer than K_MINIMUM_BYTES or more than
                 *         K_MAXIMUM_BYTES bytes.
                 */
                [[nodiscard]] static Result<SharedSecret>
                from_file(const std::filesystem::path& path);

                /**
                 * @brief Reads a secret from the file an environment variable names.
                 * @return The secret, the empty one when the variable is unset or empty, or
                 *         the error of from_file(), naming the variable as well.
                 */
                [[nodiscard]] static Result<SharedSecret>
                from_environment(const std::string& variable_name);

                /**
                 * @brief Copies a secret out of memory the caller owns.
                 * @return The secret, or InvalidAccess when @p bytes holds fewer than
                 *         K_MINIMUM_BYTES or more than K_MAXIMUM_BYTES bytes.
                 */
                [[nodiscard]] static Result<SharedSecret>
                from_bytes(span<const std::uint8_t> bytes);

                /** @brief Returns a second owner of the same bytes. */
                [[nodiscard]] SharedSecret clone() const;

                /** @brief Reports whether the secret holds no bytes. */
                [[nodiscard]] bool empty() const noexcept;

                /** @brief Returns the number of bytes held. */
                [[nodiscard]] std::size_t size() const noexcept;

                /** @brief Returns the bytes; the view dies with this object or its next move. */
                [[nodiscard]] span<const std::uint8_t> bytes() const noexcept;

            private:
                /** @brief Takes ownership of bytes whose length the caller has checked. */
                explicit SharedSecret(std::vector<std::uint8_t>&& bytes) noexcept;

                /** @brief Overwrites the bytes held with zeros and releases them. */
                void wipe() noexcept;

                /** @brief Holds the secret; sized once, so no reallocation leaves a copy. */
                std::vector<std::uint8_t> bytes_;
        };

        /** @brief Refuses to print a secret. */
        std::ostream& operator<<(std::ostream&, const SharedSecret&) = delete;

    } // namespace Distributed
} // namespace SushiRuntime
