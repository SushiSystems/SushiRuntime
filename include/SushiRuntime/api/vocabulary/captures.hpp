/****************************************************************/
/* captures.hpp                                                 */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
         * capture and dependency ordering together. Out and InOut are the same
         * wrapper for the other two access intents.
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

            /**
             * @brief Captures @p b as a read-only kernel argument.
             * @param b Buffer to read; must outlive every run that uses the task.
             */
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

            /**
             * @brief Captures @p b as a write-only kernel argument.
             * @param b Buffer to write; must outlive every run that uses the task.
             */
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

            /**
             * @brief Captures @p b as a read-and-write kernel argument.
             * @param b Buffer read and written in place; must outlive every run that
             *          uses the task.
             */
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
