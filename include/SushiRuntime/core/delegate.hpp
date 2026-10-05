/****************************************************************/
/* delegate.hpp                                                 */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file delegate.hpp
 * @brief Declares Delegate, a move-only type-erased callable stored inline.
 * @author Mustafa Garip
 * @see include/SushiRuntime/core/README.md
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Stores a move-only callable inline, with no heap allocation.
         * @tparam Sig The call signature, written as R(Args...).
         * @tparam Capacity Inline storage in bytes for the erased callable.
         * @see include/SushiRuntime/core/README.md
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
                 * @tparam F The callable type; must be invocable as R(Args...) and fit the inline
                 *        storage.
                 * @see include/SushiRuntime/core/README.md
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
                    R (*invoke)(Storage*, Args...);
                    void (*move)(Storage* dst, Storage* src) noexcept;
                    void (*destroy)(Storage*) noexcept;
                };

                template <class F>
                static R invoke_impl(Storage* s, Args... args)
                {
                    return (*reinterpret_cast<F*>(s))(std::forward<Args>(args)...);
                }

                template <class F>
                static void move_impl(Storage* dst, Storage* src) noexcept
                {
                    ::new (static_cast<void*>(dst)) F(std::move(*reinterpret_cast<F*>(src)));
                    reinterpret_cast<F*>(src)->~F();
                }

                template <class F>
                static void destroy_impl(Storage* s) noexcept
                {
                    reinterpret_cast<F*>(s)->~F();
                }

                /**
                 * @brief Returns the one shared vtable instance of a target type.
                 * @see include/SushiRuntime/core/README.md
                 */
                template <class F>
                static const VTable* vtable_for() noexcept
                {
                    static constexpr VTable vt{ &invoke_impl<F>, &move_impl<F>,
                                                &destroy_impl<F> };
                    return &vt;
                }

                void reset() noexcept
                {
                    if (vtable_)
                    {
                        vtable_->destroy(&storage_);
                        vtable_ = nullptr;
                    }
                }

                Storage storage_;
                const VTable* vtable_ = nullptr;
        };

    } // namespace Core
} // namespace SushiRuntime
