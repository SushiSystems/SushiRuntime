/**************************************************************************/
/* kernel_pointers.hpp                                                    */
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
 * @file kernel_pointers.hpp
 * @brief Declares KernelPointers, a node's kernel pointers bound on its device, and their scope.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/bound_operands.hpp>
#include <SushiRuntime/handle/element_layout.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_home_reader.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Says whether every address a node's kernel reads can be rebound to another
         *        device's instance.
         * @see modules/engine/task/README.md
         */
        enum class KernelAddressing : std::uint8_t
        {
            FixedToHome, /**< The kernel captured raw addresses; it runs only on the home device of its data. */
            Rebindable   /**< Every address the kernel reads comes from the node's KernelPointers table. */
        };

        /** @brief Describes one pointer a kernel receives: its buffer and its home instance. */
        struct KernelPointerSlot
        {
            GlobalHandle handle;                    /**< Buffer the pointer points into. */
            void* home = nullptr;                   /**< Address recorded at add(). */
            std::optional<std::size_t> home_device; /**< Device that holds the home instance; none for host or planned memory. */
            bool host_visible = false;              /**< The home instance is host memory every device reads in place. */
            ElementLayout layout{};                 /**< Layout of one element of the buffer; raw bytes until stated. */
        };

        /**
         * @brief Returns whether every device reads the home instance of @p handle in place,
         *        the one rule of host visibility.
         * @param home Where the buffer of @p handle was made and what its home instance lives in.
         * @see modules/engine/task/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API bool home_is_host_visible(const HandleHome& home,
                                                                 GlobalHandle handle) noexcept;

        /**
         * @brief Returns the slot of the pointer @p home into @p handle, read from @p directory:
         *        its element layout, its home device, and whether its home is host visible.
         * @param device_count Devices to look for the home instance on.
         * @see modules/engine/task/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API KernelPointerSlot
        make_kernel_pointer_slot(const HandleHomeReader& directory, GlobalHandle handle,
                                 void* home, std::size_t device_count);

        /**
         * @brief Returns the slot of @p handle with its home address read from @p directory: the
         *        instance in the handle's home space, or null when the home holds none yet.
         * @param device_count Devices to look for the home instance on.
         * @see modules/engine/task/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API KernelPointerSlot
        make_kernel_pointer_slot(const HandleHomeReader& directory, GlobalHandle handle,
                                 std::size_t device_count);

        /**
         * @brief Holds the pointers one node's kernel receives, rebound to the node's device at
         *        each dispatch.
         *
         * One thread dispatches a node at a time, so bind() takes no lock.
         * @see modules/engine/task/README.md
         */
        class KernelPointers final
        {
            public:
                /**
                 * @brief Builds the table of @p slots, resolved through @p resolver.
                 * @param resolver Outlives the table.
                 */
                SUSHIRUNTIME_API KernelPointers(std::vector<KernelPointerSlot> slots,
                                                const HandleResolver& resolver);

                /**
                 * @brief Points every slot at its buffer's instance on @p device.
                 *
                 * A slot whose buffer has no instance there keeps its home address only when that
                 * home is host visible; any other such slot stops the process with a message that
                 * names the handle and the device.
                 */
                SUSHIRUNTIME_API void bind(std::size_t device) noexcept;

                /** @brief Points every slot at its home address, for a node that runs with no device. */
                SUSHIRUNTIME_API void bind_home() noexcept;

                /**
                 * @brief Returns the view of the pointers of the last bind(), one per slot, in slot
                 *        order, each with the layout of its buffer's elements.
                 *
                 * The view is the one way the dispatched node's body, the API and a consumer read
                 * the table. It stays valid until the next bind().
                 */
                [[nodiscard]] BoundOperands operands() const noexcept
                {
                    return BoundOperands(bound_.data(), layouts_.data(), bound_.size());
                }

                /** @brief Returns how many bind() calls read no directory, every slot being at home. */
                [[nodiscard]] std::uint64_t home_binds() const noexcept { return home_binds_; }

                /** @brief Returns how many bind() calls resolved at least one slot through the directory. */
                [[nodiscard]] std::uint64_t resolved_binds() const noexcept { return resolved_binds_; }

            private:
                std::vector<KernelPointerSlot> slots_; /**< What each pointer stands for. */
                std::vector<void*> bound_;             /**< The pointers of the last bind(). */
                std::vector<ElementLayout> layouts_;   /**< The element layout of each slot's buffer. */
                const HandleResolver* resolver_;       /**< The directory the slots resolve through. */
                std::uint64_t home_binds_ = 0;         /**< Binds that took the home fast path. */
                std::uint64_t resolved_binds_ = 0;     /**< Binds that read the directory. */
        };

        /**
         * @brief Makes a node's bound pointers the ones the kernels built on the calling thread
         *        receive, until the scope ends. Scopes nest.
         * @see modules/engine/task/README.md
         */
        class KernelPointerScope final
        {
            public:
                /** @brief Opens the scope over @p pointers, which outlives it. */
                explicit KernelPointerScope(const KernelPointers& pointers) noexcept
                    : previous_(slot())
                {
                    slot() = &pointers;
                }

                KernelPointerScope(const KernelPointerScope&) = delete;
                KernelPointerScope& operator=(const KernelPointerScope&) = delete;
                KernelPointerScope(KernelPointerScope&&) = delete;
                KernelPointerScope& operator=(KernelPointerScope&&) = delete;

                /** @brief Restores the scope that was open before this one. */
                ~KernelPointerScope()
                {
                    slot() = previous_;
                }

                /** @brief Returns the table of the open scope of this thread, or null when none is open. */
                [[nodiscard]] static const KernelPointers* current() noexcept { return slot(); }

            private:
                /** @brief Returns the slot that holds this thread's open table. */
                SUSHIRUNTIME_API static const KernelPointers*& slot() noexcept;

                /** @brief The table that was open when this scope started. */
                const KernelPointers* previous_;
        };
    } // namespace Internal::Graph
} // namespace SushiRuntime
