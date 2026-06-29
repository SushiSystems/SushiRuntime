/**************************************************************************/
/* captures.hpp                                                           */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <type_traits>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/core/resource_liveness.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief A kernel argument captured for read, with auto dependency tracking.
         *
         * Wraps a Buffer so add() can both register the read and hand the kernel the
         * raw element pointer — the caller writes the data flow once and gets pointer
         * capture and dependency ordering together. @ref ACCESS marks the intent.
         *
         * @tparam T Element type of the wrapped buffer.
         */
        template <typename T>
        struct In
        {
            const T* ptr;  /**< Element pointer handed to the kernel. */
            void* key;     /**< Read dependency key for this argument. */
            Core::LivenessObserver life; /**< Observer of the captured buffer's lifetime. */
            static constexpr bool reads = true;   /**< Contributes a read key. */
            static constexpr bool writes = false; /**< Contributes no write key. */

            /** @brief Captures @p b as a read-only kernel argument. */
            In(const Buffer<T>& b) : ptr(b.data()), key(b.dependency_key()), life(b.liveness()) {}
        };

        /**
         * @brief A kernel argument captured for write, with auto dependency tracking.
         * @tparam T Element type of the wrapped buffer.
         */
        template <typename T>
        struct Out
        {
            T* ptr;        /**< Element pointer handed to the kernel. */
            void* key;     /**< Write dependency key for this argument. */
            Core::LivenessObserver life; /**< Observer of the captured buffer's lifetime. */
            static constexpr bool reads = false;  /**< Contributes no read key. */
            static constexpr bool writes = true;  /**< Contributes a write key. */

            /** @brief Captures @p b as a write-only kernel argument. */
            Out(Buffer<T>& b) : ptr(b.data()), key(b.dependency_key()), life(b.liveness()) {}
        };

        /**
         * @brief A kernel argument captured for read-modify-write tracking.
         * @tparam T Element type of the wrapped buffer.
         */
        template <typename T>
        struct InOut
        {
            T* ptr;        /**< Element pointer handed to the kernel. */
            void* key;     /**< Dependency key counted as both read and write. */
            Core::LivenessObserver life; /**< Observer of the captured buffer's lifetime. */
            static constexpr bool reads = true;   /**< Contributes a read key. */
            static constexpr bool writes = true;  /**< Contributes a write key. */

            /** @brief Captures @p b as a read-and-write kernel argument. */
            InOut(Buffer<T>& b) : ptr(b.data()), key(b.dependency_key()), life(b.liveness()) {}
        };

        // Class-template argument deduction so the wrapper element type follows the
        // buffer: In(buf), Out(buf), InOut(buf) need no <T>.
        template <typename T> In(const Buffer<T>&) -> In<T>;
        template <typename T> Out(Buffer<T>&) -> Out<T>;
        template <typename T> InOut(Buffer<T>&) -> InOut<T>;

        namespace detail
        {
            // True for the In/Out/InOut kernel-argument wrappers. Lets the
            // auto-capture add() overload tell its capture arguments from the
            // trailing callable.
            template <class T> struct is_capture : std::false_type {};
            template <class T> struct is_capture<In<T>>    : std::true_type {};
            template <class T> struct is_capture<Out<T>>   : std::true_type {};
            template <class T> struct is_capture<InOut<T>> : std::true_type {};
            template <class T>
            inline constexpr bool is_capture_v = is_capture<std::decay_t<T>>::value;
        } // namespace detail

    } // namespace API
} // namespace SushiRuntime
