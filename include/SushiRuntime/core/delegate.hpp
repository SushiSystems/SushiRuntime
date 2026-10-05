/****************************************************************/
/* delegate.hpp                                                 */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

/**
 * @file delegate.hpp
 * @brief A move-only, allocation-free type-erased callable (a std::function
 *        replacement for the runtime's stored closures).
 *
 * The runtime stores user callables that are polled every step (late-binding
 * predicates, per-step updaters) and replay closures built once per add(). Using
 * std::function for these costs a heap allocation per stored closure and drags in
 * RTTI and target() machinery the runtime never uses. Delegate erases the callable
 * into a fixed inline buffer with a hand-written three-entry vtable: no heap, ever.
 * A capture that does not fit is a compile error, not a silent allocation — exactly
 * the guarantee a real-time, deterministic runtime wants at its API surface.
 */

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
         * @brief A move-only callable stored inline, with no heap allocation.
         *
         * Holds any callable matching @c R(Args...) whose size and alignment fit the
         * inline storage. The callable is relocated, not reference-counted, so the
         * Delegate owns it outright and frees it on destruction. Unlike std::function
         * it is move-only (a stored closure has one owner) and never allocates: a
         * target too large for @p Capacity fails a static_assert rather than spilling
         * to the heap. Default-constructed it is empty; calling an empty Delegate is a
         * precondition violation (asserted in debug builds).
         *
         * @tparam Sig      The call signature, written as @c R(Args...).
         * @tparam Capacity Inline storage in bytes for the erased callable.
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
                 * @brief Erases @p f into inline storage.
                 *
                 * Move-constructs the decayed callable into the inline buffer and binds
                 * the static vtable for its type. The size/alignment fit is enforced at
                 * compile time, so the constructor never allocates and never fails.
                 *
                 * @tparam F The callable type; must be invocable as @c R(Args...) and
                 *           fit the inline storage.
                 * @param f  The callable to take ownership of.
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
                 *
                 * Forwards the arguments to the erased target through the vtable. Must
                 * not be called on an empty Delegate; doing so is a precondition
                 * violation, asserted in debug builds.
                 *
                 * @param args The call arguments forwarded to the target.
                 * @return Whatever the target returns.
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

                // The three operations type erasure needs: call the target, relocate it
                // into fresh storage (placement-move then destroy the source), and
                // destroy it. A single static instance per target type is shared by
                // every Delegate holding that type.
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

                // One shared vtable instance per target type, reached through a
                // function-local static so it needs no out-of-line definition and has
                // a single address across translation units.
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
