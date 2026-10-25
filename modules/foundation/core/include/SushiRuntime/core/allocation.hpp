/**************************************************************************/
/* allocation.hpp                                                         */
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
 * @file allocation.hpp
 * @brief Declares Allocation and AllocationOwner.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Releases an address that an Allocation owned.
         * @see modules/foundation/core/README.md
         */
        class AllocationOwner
        {
            public:
                /** @brief Destroys the owner. */
                virtual ~AllocationOwner() = default;

                /**
                 * @brief Returns @p address to whatever produced it.
                 * @param address The allocation being released.
                 */
                virtual void release(void* address) noexcept = 0;
        };

        /**
         * @brief Owns an address, its size, and the owner that releases it. Move-only.
         * @see modules/foundation/core/README.md
         */
        class Allocation final
        {
            public:
                /** @brief Constructs an allocation that owns nothing. */
                Allocation() noexcept = default;

                /**
                 * @brief Takes ownership of @p address.
                 * @param address The allocation's base address.
                 * @param bytes   Usable bytes at @p address.
                 * @param owner   Releases @p address exactly once; null releases nothing.
                 */
                Allocation(void* address, std::size_t bytes,
                           std::unique_ptr<AllocationOwner> owner) noexcept
                    : address_(address), bytes_(bytes), owner_(std::move(owner))
                {

                }

                /** @brief Releases the owned address, if any. */
                ~Allocation() { reset(); }

                Allocation(const Allocation&) = delete;
                Allocation& operator=(const Allocation&) = delete;

                /**
                 * @brief Takes @p other's address and leaves it empty.
                 * @param other Allocation to take the address from; left empty.
                 */
                Allocation(Allocation&& other) noexcept
                    : address_(std::exchange(other.address_, nullptr)),
                      bytes_(std::exchange(other.bytes_, 0)),
                      owner_(std::move(other.owner_))
                {

                }

                /**
                 * @brief Releases this address, then takes @p other's.
                 * @param other Allocation to take the address from; left empty.
                 * @return *this, now owning @p other's address.
                 */
                Allocation& operator=(Allocation&& other) noexcept
                {
                    if (this != &other)
                    {
                        reset();
                        address_ = std::exchange(other.address_, nullptr);
                        bytes_ = std::exchange(other.bytes_, 0);
                        owner_ = std::move(other.owner_);
                    }
                    return *this;
                }

                /**
                 * @brief Returns the owned address.
                 * @return The base address, or null when empty.
                 */
                [[nodiscard]] void* address() const noexcept { return address_; }

                /**
                 * @brief Reports the allocation's usable size.
                 * @return Usable byte count.
                 */
                [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }

                /**
                 * @brief Tells whether this handle still owns an address.
                 * @return True if it owns nothing, e.g. after being moved from.
                 */
                [[nodiscard]] bool empty() const noexcept { return address_ == nullptr; }

                /** @brief Releases the owned address and empties the allocation. */
                void reset() noexcept
                {
                    if (address_ != nullptr && owner_)
                        owner_->release(address_);
                    address_ = nullptr;
                    bytes_ = 0;
                    owner_.reset();
                }

            private:
                /** @brief Base address, or null when empty. */
                void* address_ = nullptr;

                /** @brief Usable bytes at address_. */
                std::size_t bytes_ = 0;

                /** @brief Releases address_; null for memory another owner keeps. */
                std::unique_ptr<AllocationOwner> owner_;
        };
    } // namespace Detail
} // namespace SushiRuntime
