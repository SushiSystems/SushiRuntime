/**************************************************************************/
/* delegate.hpp                                                           */
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
 * @file delegate.hpp
 * @brief Declares Delegate, a move-only type-erased callable stored inline.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace SushiRuntime
{
    /**
     * @brief Stores a move-only callable inline, with no heap allocation.
     * @tparam Sig The call signature, written as R(Args...).
     * @tparam Capacity Inline storage in bytes for the erased callable.
     * @see modules/foundation/core/README.md
     */
    template <class Sig, std::size_t Capacity = 64>
    class Delegate;

    template <class R, class... Args, std::size_t Capacity>
    class Delegate<R(Args...), Capacity>
    {
        public:
            /** @brief Constructs an empty Delegate that holds no callable. */
            Delegate() noexcept = default;

            /** @brief Constructs an empty Delegate (explicit null form). */
            Delegate(std::nullptr_t) noexcept {}

            /**
             * @brief Erases f into inline storage without allocating.
             * @tparam F The callable type; must be invocable as R(Args...), fit the inline
             *        storage and have a noexcept move constructor.
             * @see modules/foundation/core/README.md
             */
            template <class F,
                      class DF = std::decay_t<F>,
                      std::enable_if_t<!std::is_same_v<DF, Delegate> &&
                                       std::is_invocable_r_v<R, DF&, Args...>, int> = 0>
            Delegate(F&& f)
            {
                static_assert(sizeof(DF) <= Capacity,
                              "Delegate: callable capture is larger than the inline "
                              "storage; reduce what it captures or raise Capacity.");
                static_assert(alignof(DF) <= alignof(Storage),
                              "Delegate: callable alignment exceeds the inline storage "
                              "alignment.");
                static_assert(std::is_move_constructible_v<DF>,
                              "Delegate: callable must be move-constructible.");
                static_assert(std::is_nothrow_move_constructible_v<DF>,
                              "Delegate: callable must be nothrow-move-constructible; do not "
                              "capture const values or types whose move constructor can "
                              "throw.");
                ::new (static_cast<void*>(&storage_)) DF(std::forward<F>(f));
                vtable_ = vtable_for<DF>();
            }

            /** @brief Takes ownership of @p other's callable, leaving it empty. */
            Delegate(Delegate&& other) noexcept
            {
                if (other.vtable_)
                {
                    other.vtable_->move(&storage_, &other.storage_);
                    vtable_ = other.vtable_;
                    other.vtable_ = nullptr;
                }
            }

            /** @brief Destroys this callable, then takes @p other's. */
            Delegate& operator=(Delegate&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    if (other.vtable_)
                    {
                        other.vtable_->move(&storage_, &other.storage_);
                        vtable_ = other.vtable_;
                        other.vtable_ = nullptr;
                    }
                }
                return *this;
            }

            Delegate(const Delegate&) = delete;
            Delegate& operator=(const Delegate&) = delete;

            /** @brief Destroys the held callable, if any. */
            ~Delegate() { reset(); }

            /** @brief Drops any held callable, leaving the Delegate empty. */
            Delegate& operator=(std::nullptr_t) noexcept
            {
                reset();
                return *this;
            }

            /**
             * @brief Invokes the held callable.
             * @pre The Delegate is not empty.
             */
            R operator()(Args... args) const
            {
                assert(vtable_ && "Delegate: called while empty.");
                return vtable_->invoke(const_cast<Storage*>(&storage_),
                                       std::forward<Args>(args)...);
            }

            /** @brief True when the Delegate holds a callable. */
            explicit operator bool() const noexcept { return vtable_ != nullptr; }

        private:
            using Storage =
                std::aligned_storage_t<Capacity, alignof(std::max_align_t)>;

            /**
             * @brief Holds the three operations type erasure needs: call, relocate and destroy.
             */
            struct VTable
            {
                R (*invoke)(Storage*, Args...);      /**< Calls the stored callable. */
                void (*move)(Storage* dst, Storage* src) noexcept; /**< Relocates it. */
                void (*destroy)(Storage*) noexcept;  /**< Destroys it in place. */
            };

            /**
             * @brief Calls the callable stored at s; returns its result unless R is void.
             * @tparam F The callable type stored at s.
             */
            template <class F>
            static R invoke_impl(Storage* s, Args... args)
            {
                if constexpr (std::is_void_v<R>)
                {
                    (*reinterpret_cast<F*>(s))(std::forward<Args>(args)...);
                }
                else
                {
                    return (*reinterpret_cast<F*>(s))(std::forward<Args>(args)...);
                }
            }

            /**
             * @brief Move-constructs the callable at src into dst, then destroys the source.
             * @tparam F The callable type stored at src.
             */
            template <class F>
            static void move_impl(Storage* dst, Storage* src) noexcept
            {
                ::new (static_cast<void*>(dst)) F(std::move(*reinterpret_cast<F*>(src)));
                reinterpret_cast<F*>(src)->~F();
            }

            /**
             * @brief Destroys the callable stored at s in place.
             * @tparam F The callable type stored at s.
             */
            template <class F>
            static void destroy_impl(Storage* s) noexcept
            {
                reinterpret_cast<F*>(s)->~F();
            }

            /**
             * @brief Returns the one shared vtable instance of a target type.
             * @see modules/foundation/core/README.md
             */
            template <class F>
            static const VTable* vtable_for() noexcept
            {
                static constexpr VTable vt{ &invoke_impl<F>, &move_impl<F>,
                                            &destroy_impl<F> };
                return &vt;
            }

            /** @brief Destroys the held callable, if any, and leaves the Delegate empty. */
            void reset() noexcept
            {
                if (vtable_)
                {
                    vtable_->destroy(&storage_);
                    vtable_ = nullptr;
                }
            }

            Storage storage_; /**< Inline buffer for the erased callable. */

            /** @brief Operations of the held callable; null when the Delegate is empty. */
            const VTable* vtable_ = nullptr;
    };
} // namespace SushiRuntime
