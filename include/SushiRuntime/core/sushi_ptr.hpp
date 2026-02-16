/**************************************************************************/
/* sushi_ptr.hpp                                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once

#include <cstddef>
#include <utility>
#include <functional>
#include <SushiRuntime/core/ref_counted.hpp>

namespace SushiRuntime 
{
    /**
     * @brief Smart pointer for RefCounted objects.
     * 
     * @tparam T a type derived from RefCounted.
     * 
     * Sushi_ptr uses the RAII principle. It handles reference counting automatically.
     * It increases the count on copy and decreases it on destruction or reset.
     */
    template <typename T>
    class sushi_ptr 
    {
        private:
            T* ptr = nullptr;

            template <typename U>
            friend class sushi_ptr;
            
        public:
            /** @brief Creates an empty sushi_ptr. */
            sushi_ptr() noexcept : ptr(nullptr) { }

            /** @brief Creates a sushi_ptr from a raw pointer and increases reference count. */
            sushi_ptr(T* p) : ptr(p) { if (ptr) ptr->add_ref(); }
                
            /** @brief Copy constructor. Increases reference count. */
            sushi_ptr(const sushi_ptr& other) : ptr(other.ptr) { if (ptr) ptr->add_ref(); }

            /** @brief Converts from a different sushi_ptr type. Increases reference count. */
            template <typename U>
            sushi_ptr(const sushi_ptr<U>& other) : ptr(other.ptr) { if (ptr) ptr->add_ref(); }

            /** @brief Move constructor. Does not change reference count. */
            sushi_ptr(sushi_ptr&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }

            /** @brief Moves from a different sushi_ptr type. */
            template <typename U>
            sushi_ptr(sushi_ptr<U>&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }

            /** @brief Destructor. Decreases reference count and deletes object if zero. */
            ~sushi_ptr() { if (ptr) ptr->release(); }

            /** @brief Swaps the content of two sushi_ptr objects. */
            void swap(sushi_ptr& other) noexcept 
            {
                std::swap(ptr, other.ptr);
            }

            /** @brief Copy assignment operator. */
            sushi_ptr& operator=(const sushi_ptr& other) 
            {
                sushi_ptr<T> temp(other);
                this->swap(temp);
                return *this;
            }

            template <typename U>
            sushi_ptr& operator=(const sushi_ptr<U>& other) 
            {
                sushi_ptr<T> temp(other);
                this->swap(temp);
                return *this;
            }

            /** @brief Move assignment operator. */
            sushi_ptr& operator=(sushi_ptr&& other) noexcept 
            {
                sushi_ptr<T> temp(std::move(other));
                this->swap(temp);
                return *this;
            }

            template <typename U>
            sushi_ptr& operator=(sushi_ptr<U>&& other) noexcept 
            {
                sushi_ptr<T> temp(std::move(other));
                this->swap(temp);
                return *this;
            }

            /** @brief Resets the pointer to null or a new raw pointer. */
            void reset(T* p = nullptr) 
            {
                sushi_ptr<T>(p).swap(*this);
            }

            /** @brief Arrow operator for member access. */
            T* operator->() const noexcept { return ptr; }
            /** @brief Dereference operator. */
            T& operator*() const noexcept { return *ptr; }
            /** @brief Returns the underlying raw pointer. */
            T* get() const noexcept { return ptr; }
            
            /** @brief Checks if the pointer is not null. */
            explicit operator bool() const noexcept { return ptr != nullptr; }

            bool operator==(std::nullptr_t) const noexcept { return ptr == nullptr; }
            bool operator!=(std::nullptr_t) const noexcept { return ptr != nullptr; }
            bool operator==(const sushi_ptr& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const sushi_ptr& other) const noexcept { return ptr != other.ptr; }
    };

    /** @brief Global swap function for sushi_ptr. */
    template <typename T>
    void swap(sushi_ptr<T>& lhs, sushi_ptr<T>& rhs) noexcept 
    {
        lhs.swap(rhs);
    }

    /** @brief Utility function to create a new RefCounted object wrapped in a sushi_ptr. */
    template <typename T, typename... Args>
    sushi_ptr<T> make_sushi(Args&&... args) 
    {
        return sushi_ptr<T>(new T(std::forward<Args>(args)...));
    }
    
} // namespace SushiRuntime

namespace std 
{
    /** @brief Hash support for sushi_ptr to allow usage in unordered containers. */
    template <typename T>
    struct hash<SushiRuntime::sushi_ptr<T>> 
    {
        std::size_t operator()(const SushiRuntime::sushi_ptr<T>& sp) const noexcept 
        {
            return std::hash<T*>()(sp.get());
        }
    };
}