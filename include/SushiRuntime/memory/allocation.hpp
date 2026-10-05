/****************************************************************/
/* allocation.hpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief Releases an address that an Allocation owned.
         * @see docs/design/SYCL_VULKAN_INTEROP.md §3.1 in the SushiEngine repository.
         */
        class IAllocationOwner
        {
            public:
                /** @brief Destroys the owner. */
                virtual ~IAllocationOwner() = default;

                /**
                 * @brief Returns @p address to whatever produced it.
                 * @param address The allocation being released.
                 */
                virtual void release(void* address) noexcept = 0;
        };

        /**
         * @brief Owning handle to an address, its size, and the owner that
         * releases it. Move-only.
         * @see docs/design/SYCL_VULKAN_INTEROP.md §3.1 in the SushiEngine repository.
         */
        class Allocation
        {
            public:
                /** @brief Constructs an allocation that owns nothing. */
                Allocation() noexcept = default;

                /**
                 * @brief Takes ownership of @p address.
                 * @param address The allocation's base address.
                 * @param bytes   Usable bytes at @p address.
                 * @param owner   Releases @p address exactly once.
                 */
                Allocation(void* address, std::size_t bytes,
                           std::unique_ptr<IAllocationOwner> owner) noexcept
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
                    : address_(other.address_), bytes_(other.bytes_),
                      owner_(std::move(other.owner_))
                {
                    other.address_ = nullptr;
                    other.bytes_ = 0;
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
                        address_ = other.address_;
                        bytes_ = other.bytes_;
                        owner_ = std::move(other.owner_);
                        other.address_ = nullptr;
                        other.bytes_ = 0;
                    }
                    return *this;
                }

                /**
                 * @brief Returns the owned address.
                 * @return The base address, or null when empty.
                 */
                void* address() const noexcept { return address_; }

                /**
                 * @brief Reports the allocation's usable size.
                 * @return Usable byte count.
                 */
                std::size_t bytes() const noexcept { return bytes_; }

                /**
                 * @brief Tells whether this handle still owns an address.
                 * @return True if it owns nothing, e.g. after being moved from.
                 */
                bool empty() const noexcept { return address_ == nullptr; }

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
                void* address_ = nullptr;
                std::size_t bytes_ = 0;
                std::unique_ptr<IAllocationOwner> owner_;
        };

    } // namespace Memory
} // namespace SushiRuntime
