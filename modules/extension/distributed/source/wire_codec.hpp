/**************************************************************************/
/* wire_codec.hpp                                                         */
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
 * @file wire_codec.hpp
 * @brief Declares FieldWriter and FieldReader, the little-endian field codec of the wire format.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include <SushiRuntime/core/span.hpp>

namespace SushiRuntime::Distributed::Wire
{
    /** @brief Holds the largest number of bytes a string field may carry. */
    inline constexpr std::size_t K_MAX_STRING_BYTES = 255;

    /**
     * @brief Names the outcome of encoding or decoding the fields of one payload.
     */
    enum class CodecStatus : std::uint8_t
    {
        Ok,            /**< Every field was written or read. */
        Truncated,     /**< The payload ended before the field did. */
        OutOfRange,    /**< A bool, an enumeration, a length or a count held a refused value. */
        TrailingBytes, /**< Bytes remained after the last field. */
        TooLong,       /**< A value to encode exceeds what its field or a frame can carry. */
    };

    /**
     * @brief Holds the outcome of decoding one payload and where it failed.
     */
    struct DecodeResult
    {
        /** @brief Holds the first failure, or Ok. */
        CodecStatus status = CodecStatus::Ok;

        /** @brief Holds the payload offset of the field that failed; 0 on success. */
        std::size_t offset = 0;

        /** @brief Returns whether the payload decoded. */
        [[nodiscard]] constexpr bool ok() const noexcept
        {
            return status == CodecStatus::Ok;
        }
    };

    /**
     * @brief Appends little-endian fields to a byte buffer the caller owns and reuses.
     *
     * A value that does not fit its field is not written and sets the status, which stays
     * at the first failure.
     */
    class FieldWriter final
    {
        public:
            /** @brief Binds the writer to the buffer it appends to; the buffer must outlive it. */
            explicit FieldWriter(std::vector<std::byte>& out) noexcept : out_(out)
            {

            }

            FieldWriter(const FieldWriter&) = delete;
            FieldWriter(FieldWriter&&) = delete;
            FieldWriter& operator=(const FieldWriter&) = delete;
            FieldWriter& operator=(FieldWriter&&) = delete;
            ~FieldWriter() = default;

            /** @brief Appends one byte. */
            void put_u8(std::uint8_t value)
            {
                out_.push_back(static_cast<std::byte>(value));
            }

            /** @brief Appends a 16-bit unsigned integer. */
            void put_u16(std::uint16_t value)
            {
                put_little_endian<std::uint16_t>(value);
            }

            /** @brief Appends a 32-bit unsigned integer. */
            void put_u32(std::uint32_t value)
            {
                put_little_endian<std::uint32_t>(value);
            }

            /** @brief Appends a 64-bit unsigned integer. */
            void put_u64(std::uint64_t value)
            {
                put_little_endian<std::uint64_t>(value);
            }

            /** @brief Appends a 32-bit signed integer in two's complement. */
            void put_i32(std::int32_t value)
            {
                put_little_endian<std::uint32_t>(static_cast<std::uint32_t>(value));
            }

            /** @brief Appends a bool as one byte, 0 or 1. */
            void put_bool(bool value)
            {
                put_u8(value ? 1 : 0);
            }

            /** @brief Appends an enumerator as its one-byte value. */
            template <typename Enum>
            void put_enum(Enum value)
            {
                static_assert(sizeof(Enum) == 1, "a wire enumeration is one byte");
                put_u8(static_cast<std::uint8_t>(value));
            }

            /** @brief Appends raw bytes with no length in front. */
            void put_bytes(SushiRuntime::span<const std::byte> bytes)
            {
                out_.insert(out_.end(), bytes.begin(), bytes.end());
            }

            /** @brief Appends a string as a 16-bit length and its bytes; fails above 255 bytes. */
            void put_string(std::string_view text)
            {
                if (text.size() > K_MAX_STRING_BYTES)
                {
                    fail(CodecStatus::TooLong);
                    return;
                }
                put_u16(static_cast<std::uint16_t>(text.size()));
                const auto* first = reinterpret_cast<const std::byte*>(text.data());
                out_.insert(out_.end(), first, first + text.size());
            }

            /** @brief Appends the 32-bit element count of a list; fails above 2^32 - 1. */
            void put_count(std::size_t count)
            {
                if (count > std::numeric_limits<std::uint32_t>::max())
                {
                    fail(CodecStatus::TooLong);
                    return;
                }
                put_u32(static_cast<std::uint32_t>(count));
            }

            /** @brief Records a failure unless one is already recorded. */
            void fail(CodecStatus status) noexcept
            {
                if (status_ == CodecStatus::Ok)
                    status_ = status;
            }

            /** @brief Returns the first failure, or Ok. */
            [[nodiscard]] CodecStatus status() const noexcept
            {
                return status_;
            }

        private:
            /** @brief Appends an unsigned integer, least significant byte first. */
            template <typename Unsigned>
            void put_little_endian(Unsigned value)
            {
                std::byte bytes[sizeof(Unsigned)];
                for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                    bytes[index] = static_cast<std::byte>((value >> (8 * index)) & 0xFFu);
                out_.insert(out_.end(), bytes, bytes + sizeof(Unsigned));
            }

            /// @brief Buffer the fields are appended to.
            std::vector<std::byte>& out_;

            /// @brief First failure, or Ok.
            CodecStatus status_ = CodecStatus::Ok;
    };

    /**
     * @brief Reads little-endian fields from a payload with bounds and range checks.
     *
     * A failed read records the status and its offset, and every later read returns zero
     * without advancing, so a decoder tests the result once through finish(). No read
     * allocates; strings and the rest of the payload come back as views into it.
     */
    class FieldReader final
    {
        public:
            /** @brief Binds the reader to a payload, which must outlive it and its views. */
            explicit FieldReader(SushiRuntime::span<const std::byte> data) noexcept : data_(data)
            {

            }

            /** @brief Reads one byte. */
            [[nodiscard]] std::uint8_t read_u8() noexcept
            {
                const std::byte* bytes = take(1);
                return bytes == nullptr ? std::uint8_t{0} : static_cast<std::uint8_t>(bytes[0]);
            }

            /** @brief Reads one byte; a value above maximum fails with OutOfRange and reads 0. */
            [[nodiscard]] std::uint8_t read_u8_at_most(std::uint8_t maximum) noexcept
            {
                const std::size_t start = position_;
                const std::uint8_t value = read_u8();
                if (value > maximum)
                {
                    fail(CodecStatus::OutOfRange, start);
                    return 0;
                }
                return value;
            }

            /** @brief Reads a 16-bit unsigned integer. */
            [[nodiscard]] std::uint16_t read_u16() noexcept
            {
                return read_little_endian<std::uint16_t>();
            }

            /** @brief Reads a 32-bit unsigned integer. */
            [[nodiscard]] std::uint32_t read_u32() noexcept
            {
                return read_little_endian<std::uint32_t>();
            }

            /** @brief Reads a 64-bit unsigned integer. */
            [[nodiscard]] std::uint64_t read_u64() noexcept
            {
                return read_little_endian<std::uint64_t>();
            }

            /** @brief Reads a 32-bit signed integer in two's complement. */
            [[nodiscard]] std::int32_t read_i32() noexcept
            {
                const std::uint32_t bits = read_little_endian<std::uint32_t>();
                std::int32_t value = 0;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            /** @brief Reads a bool; a byte other than 0 or 1 fails with OutOfRange. */
            [[nodiscard]] bool read_bool() noexcept
            {
                const std::size_t start = position_;
                const std::uint8_t value = read_u8();
                if (value > 1)
                {
                    fail(CodecStatus::OutOfRange, start);
                    return false;
                }
                return value == 1;
            }

            /**
             * @brief Reads a one-byte enumerator; a value outside [first, last] fails with
             *        OutOfRange.
             * @return The enumerator, or first after a failure.
             */
            template <typename Enum>
            [[nodiscard]] Enum read_enum(Enum first, Enum last) noexcept
            {
                static_assert(sizeof(Enum) == 1, "a wire enumeration is one byte");
                const std::size_t start = position_;
                const std::uint8_t value = read_u8();
                if (!ok())
                    return first;
                if (value < static_cast<std::uint8_t>(first)
                    || value > static_cast<std::uint8_t>(last))
                {
                    fail(CodecStatus::OutOfRange, start);
                    return first;
                }
                return static_cast<Enum>(value);
            }

            /** @brief Copies the next out.size() bytes into out; out is left alone on failure. */
            void read_bytes(SushiRuntime::span<std::byte> out) noexcept
            {
                const std::byte* bytes = take(out.size());
                if (bytes != nullptr && !out.empty())
                    std::memcpy(out.data(), bytes, out.size());
            }

            /**
             * @brief Reads a string; a length above 255 fails with OutOfRange.
             * @return A view into the payload, empty after a failure.
             */
            [[nodiscard]] std::string_view read_string() noexcept
            {
                const std::size_t start = position_;
                const std::uint16_t length = read_u16();
                if (length > K_MAX_STRING_BYTES)
                {
                    fail(CodecStatus::OutOfRange, start);
                    return {};
                }
                const std::byte* bytes = take(length);
                if (bytes == nullptr)
                    return {};
                return { reinterpret_cast<const char*>(bytes), length };
            }

            /**
             * @brief Reads a list's element count and checks it against the bytes that remain.
             * @param element_bytes Smallest encoded size of one element; must be positive.
             * @return The count, or 0 when the payload cannot hold that many elements.
             */
            [[nodiscard]] std::uint32_t read_count(std::size_t element_bytes) noexcept
            {
                const std::size_t start = position_;
                const std::uint32_t count = read_u32();
                if (count > remaining() / element_bytes)
                {
                    fail(CodecStatus::OutOfRange, start);
                    return 0;
                }
                return count;
            }

            /** @brief Returns a view of every byte not yet read and consumes them. */
            [[nodiscard]] SushiRuntime::span<const std::byte> read_rest() noexcept
            {
                const std::size_t count = remaining();
                const std::byte* bytes = take(count);
                if (bytes == nullptr)
                    return {};
                return { bytes, count };
            }

            /** @brief Returns the number of bytes not yet read; 0 after a failure. */
            [[nodiscard]] std::size_t remaining() const noexcept
            {
                return ok() ? data_.size() - position_ : 0;
            }

            /** @brief Returns whether every read so far succeeded. */
            [[nodiscard]] bool ok() const noexcept
            {
                return status_ == CodecStatus::Ok;
            }

            /** @brief Records a failure and its offset unless one is already recorded. */
            void fail(CodecStatus status, std::size_t offset) noexcept
            {
                if (status_ != CodecStatus::Ok)
                    return;
                status_ = status;
                failure_offset_ = offset;
            }

            /** @brief Returns the outcome of the whole payload; unread bytes fail it. */
            [[nodiscard]] DecodeResult finish() noexcept
            {
                if (ok() && position_ != data_.size())
                    fail(CodecStatus::TrailingBytes, position_);
                return { status_, failure_offset_ };
            }

        private:
            /**
             * @brief Consumes a run of bytes.
             * @return The first byte of the run, or nullptr when the payload is too short or
             *         a failure is recorded.
             */
            [[nodiscard]] const std::byte* take(std::size_t count) noexcept
            {
                if (!ok())
                    return nullptr;
                if (count > data_.size() - position_)
                {
                    fail(CodecStatus::Truncated, position_);
                    return nullptr;
                }
                const std::byte* bytes = data_.data() + position_;
                position_ += count;
                return bytes;
            }

            /** @brief Reads an unsigned integer, least significant byte first. */
            template <typename Unsigned>
            [[nodiscard]] Unsigned read_little_endian() noexcept
            {
                const std::byte* bytes = take(sizeof(Unsigned));
                if (bytes == nullptr)
                    return 0;
                std::uint64_t value = 0;
                for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                    value |= static_cast<std::uint64_t>(bytes[index]) << (8 * index);
                return static_cast<Unsigned>(value);
            }

            /// @brief Payload being read.
            SushiRuntime::span<const std::byte> data_;

            /// @brief Offset of the next unread byte.
            std::size_t position_ = 0;

            /// @brief Offset of the field that failed first.
            std::size_t failure_offset_ = 0;

            /// @brief First failure, or Ok.
            CodecStatus status_ = CodecStatus::Ok;
    };
} // namespace SushiRuntime::Distributed::Wire
