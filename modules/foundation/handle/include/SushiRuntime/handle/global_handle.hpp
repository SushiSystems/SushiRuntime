/**************************************************************************/
/* global_handle.hpp                                                      */
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
 * @file global_handle.hpp
 * @brief Declares GlobalHandle, the 128-bit identity of a buffer on every device and machine.
 * @see docs/design/GLOBAL_HANDLE/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace SushiRuntime
{
    /** @brief Names what a handle identifies. */
    enum class HandleKind : std::uint8_t
    {
        Buffer = 0,  /**< A buffer. */
        State = 1,   /**< A state object. */
        Image = 2,   /**< An image. */
        Reserved = 3 /**< Kept for a later kind; no handle is minted with it. */
    };

    /** @brief Holds the flag bits a handle carries; values combine with operator|. */
    enum class HandleFlags : std::uint8_t
    {
        None = 0,        /**< No flag. */
        DoNotCache = 1,  /**< A reader keeps no copy of the resolved address. */
        Borrowed = 2,    /**< The memory belongs to the caller, not to the runtime. */
        HostOnly = 4     /**< The data never leaves host memory. */
    };

    /** @brief Returns the union of two flag sets. */
    [[nodiscard]] constexpr HandleFlags operator|(HandleFlags left, HandleFlags right) noexcept
    {
        return static_cast<HandleFlags>(static_cast<std::uint8_t>(left) |
                                        static_cast<std::uint8_t>(right));
    }

    /** @brief Returns the intersection of two flag sets. */
    [[nodiscard]] constexpr HandleFlags operator&(HandleFlags left, HandleFlags right) noexcept
    {
        return static_cast<HandleFlags>(static_cast<std::uint8_t>(left) &
                                        static_cast<std::uint8_t>(right));
    }

    /** @brief Returns whether every bit of wanted is set in flags. */
    [[nodiscard]] constexpr bool has_flags(HandleFlags flags, HandleFlags wanted) noexcept
    {
        return (flags & wanted) == wanted;
    }

    /**
     * @brief Names one buffer, state or image across devices and machines; never a location.
     *
     * Generation 0 is never minted, so the default value is the null handle.
     */
    class GlobalHandle final
    {
        public:
            /** @brief Builds the null handle. */
            constexpr GlobalHandle() noexcept = default;

            /** @brief Builds a handle from its six fields. */
            constexpr GlobalHandle(std::uint32_t universe, std::uint16_t machine, HandleKind kind,
                                   HandleFlags flags, std::uint32_t generation,
                                   std::uint32_t slot) noexcept
                : universe_(universe), machine_(machine), kind_(kind), flags_(flags),
                  generation_(generation), slot_(slot)
            {

            }

            /** @brief Returns the null handle. */
            [[nodiscard]] static constexpr GlobalHandle null() noexcept
            {
                return GlobalHandle();
            }

            /**
             * @brief Rebuilds a handle from the two words to_high_word() and to_low_word() give.
             * @param high universe in bits 63..32, machine 31..16, kind 15..8, flags 7..0.
             * @param low generation in bits 63..32, slot 31..0.
             */
            [[nodiscard]] static constexpr GlobalHandle from_words(std::uint64_t high,
                                                                   std::uint64_t low) noexcept
            {
                return GlobalHandle(static_cast<std::uint32_t>(high >> 32),
                                    static_cast<std::uint16_t>(high >> 16),
                                    static_cast<HandleKind>(static_cast<std::uint8_t>(high >> 8)),
                                    static_cast<HandleFlags>(static_cast<std::uint8_t>(high)),
                                    static_cast<std::uint32_t>(low >> 32),
                                    static_cast<std::uint32_t>(low));
            }

            /** @brief Returns universe, machine, kind and flags packed into one word. */
            [[nodiscard]] constexpr std::uint64_t to_high_word() const noexcept
            {
                return (static_cast<std::uint64_t>(universe_) << 32) |
                       (static_cast<std::uint64_t>(machine_) << 16) |
                       (static_cast<std::uint64_t>(static_cast<std::uint8_t>(kind_)) << 8) |
                       static_cast<std::uint64_t>(static_cast<std::uint8_t>(flags_));
            }

            /** @brief Returns generation and slot packed into one word. */
            [[nodiscard]] constexpr std::uint64_t to_low_word() const noexcept
            {
                return (static_cast<std::uint64_t>(generation_) << 32) |
                       static_cast<std::uint64_t>(slot_);
            }

            /** @brief Returns whether this is the null handle. */
            [[nodiscard]] constexpr bool is_null() const noexcept
            {
                return generation_ == 0;
            }

            /** @brief Returns the runtime instance that minted the handle. */
            [[nodiscard]] constexpr std::uint32_t universe() const noexcept
            {
                return universe_;
            }

            /** @brief Returns the machine id; 0 on a single machine. */
            [[nodiscard]] constexpr std::uint16_t machine() const noexcept
            {
                return machine_;
            }

            /** @brief Returns what the handle identifies. */
            [[nodiscard]] constexpr HandleKind kind() const noexcept
            {
                return kind_;
            }

            /** @brief Returns the flag bits. */
            [[nodiscard]] constexpr HandleFlags flags() const noexcept
            {
                return flags_;
            }

            /** @brief Returns the reuse count of the slot when the handle was minted. */
            [[nodiscard]] constexpr std::uint32_t generation() const noexcept
            {
                return generation_;
            }

            /** @brief Returns the index into the residency directory. */
            [[nodiscard]] constexpr std::uint32_t slot() const noexcept
            {
                return slot_;
            }

            /**
             * @brief Returns whether this handle orders before other.
             *
             * Fields compare in this order: universe, machine, slot, generation, kind, flags.
             */
            [[nodiscard]] constexpr bool operator<(const GlobalHandle& other) const noexcept
            {
                if (universe_ != other.universe_)
                    return universe_ < other.universe_;
                if (machine_ != other.machine_)
                    return machine_ < other.machine_;
                if (slot_ != other.slot_)
                    return slot_ < other.slot_;
                if (generation_ != other.generation_)
                    return generation_ < other.generation_;
                if (kind_ != other.kind_)
                    return kind_ < other.kind_;
                return flags_ < other.flags_;
            }

            /** @brief Returns whether every field equals other's. */
            [[nodiscard]] constexpr bool operator==(const GlobalHandle& other) const noexcept
            {
                return to_high_word() == other.to_high_word() &&
                       to_low_word() == other.to_low_word();
            }

            /** @brief Returns whether any field differs from other's. */
            [[nodiscard]] constexpr bool operator!=(const GlobalHandle& other) const noexcept
            {
                return !(*this == other);
            }

            /** @brief Returns whether other orders before this handle. */
            [[nodiscard]] constexpr bool operator>(const GlobalHandle& other) const noexcept
            {
                return other < *this;
            }

            /** @brief Returns whether this handle does not order after other. */
            [[nodiscard]] constexpr bool operator<=(const GlobalHandle& other) const noexcept
            {
                return !(other < *this);
            }

            /** @brief Returns whether this handle does not order before other. */
            [[nodiscard]] constexpr bool operator>=(const GlobalHandle& other) const noexcept
            {
                return !(*this < other);
            }

        private:
            /// @brief Runtime instance that minted the handle.
            std::uint32_t universe_ = 0;

            /// @brief Machine id; 0 on a single machine.
            std::uint16_t machine_ = 0;

            /// @brief What the handle identifies.
            HandleKind kind_ = HandleKind::Buffer;

            /// @brief Flag bits.
            HandleFlags flags_ = HandleFlags::None;

            /// @brief Reuse count of the slot; 0 only in the null handle.
            std::uint32_t generation_ = 0;

            /// @brief Index into the residency directory.
            std::uint32_t slot_ = 0;
    };

    static_assert(sizeof(GlobalHandle) == 16, "a GlobalHandle is 128 bits");
    static_assert(std::is_trivially_copyable<GlobalHandle>::value,
                  "a GlobalHandle is copied as bytes");
} // namespace SushiRuntime

namespace std
{
    /** @brief Hashes a GlobalHandle with the same result on every platform and run. */
    template <>
    struct hash<SushiRuntime::GlobalHandle>
    {
        /** @brief Returns the hash of handle, mixing both words with the splitmix64 finaliser. */
        [[nodiscard]] std::size_t
        operator()(const SushiRuntime::GlobalHandle& handle) const noexcept
        {
            std::uint64_t value =
                (handle.to_high_word() * 0x9E3779B97F4A7C15ULL) ^ handle.to_low_word();
            value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
            value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
            return static_cast<std::size_t>(value ^ (value >> 31));
        }
    };
} // namespace std
