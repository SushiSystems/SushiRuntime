/**************************************************************************/
/* captures.hpp                                                           */
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
 * @file captures.hpp
 * @brief Declares the In, Out and InOut kernel-argument wrappers.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <type_traits>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/memory/resource_liveness.hpp>

namespace SushiRuntime
{
    /**
     * @brief Wraps a Buffer captured for read so add() registers it and passes its pointer.
     * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
     */
    template <typename T>
    struct In
    {
        const T* ptr;  /**< Element pointer handed to the kernel. */
        void* key;     /**< Read dependency key for this argument. */
        Internal::Memory::LivenessObserver life; /**< Observer of the captured buffer's lifetime. */
        static constexpr bool reads = true;   /**< Contributes a read key. */
        static constexpr bool writes = false; /**< Contributes no write key. */

        /**
         * @brief Captures @p b as a read-only kernel argument.
         * @param b Buffer to read. Alive when the graph runs; free to move after add().
         */
        In(const Buffer<T>& b)
            : ptr(b.data()),
              key(Detail::HandleAccess::key(b)),
              life(Detail::HandleAccess::liveness(b))
        {

        }
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
        Internal::Memory::LivenessObserver life; /**< Observer of the captured buffer's lifetime. */
        static constexpr bool reads = false;  /**< Contributes no read key. */
        static constexpr bool writes = true;  /**< Contributes a write key. */

        /**
         * @brief Captures @p b as a write-only kernel argument.
         * @param b Buffer to write. Alive when the graph runs; free to move after add().
         */
        Out(Buffer<T>& b)
            : ptr(b.data()),
              key(Detail::HandleAccess::key(b)),
              life(Detail::HandleAccess::liveness(b))
        {

        }
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
        Internal::Memory::LivenessObserver life; /**< Observer of the captured buffer's lifetime. */
        static constexpr bool reads = true;   /**< Contributes a read key. */
        static constexpr bool writes = true;  /**< Contributes a write key. */

        /**
         * @brief Captures @p b as a read-and-write kernel argument.
         * @param b Buffer read and written in place. Alive when the graph runs;
         *          free to move after add().
         */
        InOut(Buffer<T>& b)
            : ptr(b.data()),
              key(Detail::HandleAccess::key(b)),
              life(Detail::HandleAccess::liveness(b))
        {

        }
    };

    // The guides let In(buf), Out(buf) and InOut(buf) omit the element type.
    template <typename T> In(const Buffer<T>&) -> In<T>;
    template <typename T> Out(Buffer<T>&) -> Out<T>;
    template <typename T> InOut(Buffer<T>&) -> InOut<T>;

    namespace Detail
    {
        /** @brief Tells the In/Out/InOut capture wrappers apart from the trailing callable. */
        template <class T> struct is_capture : std::false_type {};
        template <class T> struct is_capture<In<T>>    : std::true_type {};
        template <class T> struct is_capture<Out<T>>   : std::true_type {};
        template <class T> struct is_capture<InOut<T>> : std::true_type {};
        template <class T>
        inline constexpr bool is_capture_v = is_capture<std::decay_t<T>>::value;
    } // namespace Detail



} // namespace SushiRuntime
