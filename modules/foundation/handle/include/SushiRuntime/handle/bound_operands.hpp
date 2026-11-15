/**************************************************************************/
/* bound_operands.hpp                                                     */
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
 * @file bound_operands.hpp
 * @brief Declares BoundOperands, the one typed view of the buffers a task bound by handle.
 * @see modules/foundation/handle/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <SushiRuntime/handle/element_layout.hpp>

namespace SushiRuntime
{
    /** @brief Names why a bound operand was or was not handed out. */
    enum class OperandStatus : std::uint8_t
    {
        Ok,              /**< The operand was handed out; a null address means its buffer has no instance. */
        OutOfRange,      /**< The position is not below the number of operands. */
        ElementMismatch, /**< The element type differs in size or alignment from the buffer's element. */
        Misaligned       /**< The byte offset does not land on an element boundary of the type asked for. */
    };

    /**
     * @brief Holds the address of one bound operand, or the reason it was refused.
     * @tparam T The element type the caller asked for.
     */
    template <class T>
    struct OperandAddress
    {
        T* address = nullptr;                /**< The address; null unless status is Ok. */
        OperandStatus status = OperandStatus::Ok; /**< Ok, or why the operand was refused. */

        /** @brief Returns whether the operand was handed out. */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return status == OperandStatus::Ok;
        }
    };

    /**
     * @brief Views the buffers the task being dispatched bound, in the order the task named them,
     *        each with the layout of its elements.
     *
     * A buffer of raw bytes is viewed as any trivially copyable type at an aligned offset; any other
     * buffer only as a type of its own size and alignment. The view is valid during one dispatch.
     * @see modules/foundation/handle/README.md
     */
    class BoundOperands final
    {
        public:
            /** @brief Views no operand. */
            BoundOperands() noexcept = default;

            /**
             * @brief Views @p count operands.
             * @param table The address of each operand's buffer.
             * @param layouts The element layout of each operand; both arrays hold @p count entries
             *        and outlive the view.
             */
            BoundOperands(void* const* table, const ElementLayout* layouts, std::size_t count) noexcept
                : table_(table), layouts_(layouts), count_(count)
            {

            }

            /**
             * @brief Views @p count operands that are all raw byte buffers.
             * @param table The address of each operand's buffer; outlives the view.
             */
            [[nodiscard]] static BoundOperands of_bytes(void* const* table, std::size_t count) noexcept
            {
                return BoundOperands(table, nullptr, count);
            }

            /** @brief Returns the number of operands. */
            [[nodiscard]] std::size_t size() const noexcept { return count_; }

            /** @brief Returns whether the view holds no operand. */
            [[nodiscard]] bool is_empty() const noexcept { return count_ == 0; }

            /** @brief Returns the element layout of operand @p position. @pre position is below size(). */
            [[nodiscard]] ElementLayout layout_of(std::size_t position) const noexcept
            {
                return layouts_ == nullptr ? ElementLayout() : layouts_[position];
            }

            /**
             * @brief Returns the address of operand @p position, offset by @p byte_offset, as a T*.
             * @tparam T A trivially copyable element type, with or without const.
             * @param byte_offset Bytes from the start of the buffer to the element.
             * @return The address and Ok; a null address with Ok when the buffer has no instance;
             *         otherwise a null address and the reason: OutOfRange, ElementMismatch or
             *         Misaligned.
             */
            template <class T>
            [[nodiscard]] OperandAddress<T> at(std::size_t position,
                                               std::size_t byte_offset = 0) const noexcept
            {
                using Element = std::remove_cv_t<T>;
                static_assert(!std::is_void_v<Element> && std::is_trivially_copyable_v<Element>,
                              "An operand is viewed as a trivially copyable element type.");

                if (position >= count_)
                    return OperandAddress<T>{nullptr, OperandStatus::OutOfRange};
                const ElementLayout layout = layout_of(position);
                const bool bytes = layout.is_bytes();
                if (!bytes && (layout.size != sizeof(Element) || layout.alignment != alignof(Element)))
                    return OperandAddress<T>{nullptr, OperandStatus::ElementMismatch};
                if (byte_offset % (bytes ? alignof(Element) : layout.size) != 0)
                    return OperandAddress<T>{nullptr, OperandStatus::Misaligned};
                unsigned char* const base = static_cast<unsigned char*>(table_[position]);
                if (base == nullptr)
                    return OperandAddress<T>{nullptr, OperandStatus::Ok};
                void* const address = base + byte_offset;
                if (bytes && reinterpret_cast<std::uintptr_t>(address) % alignof(Element) != 0)
                    return OperandAddress<T>{nullptr, OperandStatus::Misaligned};
                return OperandAddress<T>{static_cast<T*>(address), OperandStatus::Ok};
            }

            /**
             * @brief Returns the address of operand @p position with no type check.
             *
             * For a launch that holds its own pointer types and recorded them at record time.
             * @pre position is below size().
             */
            [[nodiscard]] void* untyped(std::size_t position) const noexcept
            {
                return table_[position];
            }

        private:
            /// @brief The address of each operand's buffer.
            void* const* table_ = nullptr;

            /// @brief The element layout of each operand, or null when every operand is raw bytes.
            const ElementLayout* layouts_ = nullptr;

            /// @brief Number of operands.
            std::size_t count_ = 0;
    };
} // namespace SushiRuntime
