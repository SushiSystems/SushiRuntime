/**************************************************************************/
/* sushi_ptr.hpp                                                          */
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

#include <cstddef>
#include <utility>
#include <functional>
#include <SushiRuntime/core/ref_counted.hpp>

namespace SushiRuntime 
{
    /**
     * @brief An intrusive smart pointer for reference-counted objects.
     * 
     * Automatically increments and decrements the internal reference count of objects using RAII.
     * 
     * @tparam T The underlying object type, which must inherit from RefCounted.
     */
    template <typename T>
    class sushi_ptr 
    {
        private:
            T* ptr = nullptr;

            template <typename U>
            friend class sushi_ptr;
            
        public:
            /**
             * @brief Constructs an empty smart pointer.
             * 
             * Initializes the underlying pointer to null.
             */
            sushi_ptr() noexcept : ptr(nullptr) { }

            /**
             * @brief Constructs a smart pointer taking ownership of a raw pointer.
             * 
             * Increments the target object's reference count if the pointer is not null.
             * 
             * @param p The raw pointer to manage.
             */
            sushi_ptr(T* p) : ptr(p) { if (ptr) ptr->add_ref(); }
                
            /**
             * @brief Constructs a copy of another smart pointer.
             * 
             * Increments the reference count of the shared object.
             * 
             * @param other The existing smart pointer to copy.
             */
            sushi_ptr(const sushi_ptr& other) : ptr(other.ptr)          { if (ptr) ptr->add_ref(); }

            /**
             * @brief Constructs a copy from a smart pointer of a different type.
             * 
             * Increments the reference count of the shared object.
             * 
             * @tparam U The type of the other smart pointer's managed object.
             * @param other The existing smart pointer to copy from.
             */
            template <typename U>
            sushi_ptr(const sushi_ptr<U>& other) : ptr(other.ptr)       { if (ptr) ptr->add_ref(); }

            /**
             * @brief Moves an existing smart pointer into a new instance.
             * 
             * Transfers ownership without modifying the reference count.
             * 
             * @param other The smart pointer to move from.
             */
            sushi_ptr(sushi_ptr&& other) noexcept : ptr(other.ptr)      { other.ptr = nullptr; }

            /**
             * @brief Moves an existing smart pointer of a different type into a new instance.
             * 
             * Transfers ownership without modifying the reference count.
             * 
             * @tparam U The type of the other smart pointer's managed object.
             * @param other The smart pointer to move from.
             */
            template <typename U>
            sushi_ptr(sushi_ptr<U>&& other) noexcept : ptr(other.ptr)   { other.ptr = nullptr; }

            /**
             * @brief Destroys the smart pointer and releases its reference.
             * 
             * Decrements the target object's reference count and cleans it up if the count reaches zero.
             */
            ~sushi_ptr() { if (ptr) ptr->release(); }

            /**
             * @brief Swaps the managed pointers of two smart pointers.
             * 
             * Exchanges the underlying raw pointers without modifying their reference counts.
             * 
             * @param other The other smart pointer to swap with.
             */
            void swap(sushi_ptr& other) noexcept 
            {
                std::swap(ptr, other.ptr);
            }

            /**
             * @brief Assigns a new object by copying another smart pointer.
             * 
             * Modifies the reference counts of the old and new managed objects accordingly.
             * 
             * @param other The smart pointer to copy from.
             * @return A reference to this updated smart pointer.
             */
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

            /**
             * @brief Assigns a new object by moving another smart pointer.
             * 
             * Transfers ownership cleanly and releases the old managed object.
             * 
             * @param other The smart pointer to move from.
             * @return A reference to this updated smart pointer.
             */
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

            /**
             * @brief Resets the managed pointer to a new value.
             * 
             * Releases the old object and assumes ownership of the new one.
             * 
             * @param p The new raw pointer to manage, defaults to null.
             */
            void reset(T* p = nullptr) 
            {
                sushi_ptr<T>(p).swap(*this);
            }

            /**
             * @brief Accesses a member of the managed object.
             * 
             * Evaluates the underlying pointer directly.
             * 
             * @return The underlying raw pointer.
             */
            T* operator->() const noexcept  { return ptr; }
            /**
             * @brief Dereferences the managed object.
             * 
             * Evaluates the underlying pointer directly.
             * 
             * @return A reference to the managed object.
             */
            T& operator*() const noexcept   { return *ptr; }
            /**
             * @brief Retrieves the raw pointer managed by this object.
             * 
             * Returns the unmanaged address of the object.
             * 
             * @return The raw pointer.
             */
            T* get() const noexcept         { return ptr; }

            /**
             * @brief Detaches the underlying pointer from this smart pointer.
             * 
             * Transfers manual ownership to the caller without modifying the object's reference count.
             * 
             * @return The underlying raw pointer previously managed by this object.
             */
            T* leak() noexcept
            {
                T* p = ptr;
                ptr = nullptr;
                return p;
            }
            
            /**
             * @brief Checks if the smart pointer manages an object.
             * 
             * Tests the underlying pointer against null.
             * 
             * @return True if the pointer is not null, false otherwise.
             */
            explicit operator bool() const noexcept { return ptr != nullptr; }

            bool operator==(std::nullptr_t) const noexcept { return ptr == nullptr; }
            bool operator!=(std::nullptr_t) const noexcept { return ptr != nullptr; }
            bool operator==(const sushi_ptr& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const sushi_ptr& other) const noexcept { return ptr != other.ptr; }
    };

    /**
     * @brief Exchanges the managed objects of two smart pointers.
     * 
     * Uses the member swap method to efficiently trade ownership states.
     * 
     * @tparam T The type of the managed object.
     * @param lhs The first smart pointer.
     * @param rhs The second smart pointer.
     */
    template <typename T>
    void swap(sushi_ptr<T>& lhs, sushi_ptr<T>& rhs) noexcept 
    {
        lhs.swap(rhs);
    }

    /**
     * @brief Allocates and constructs a reference-counted object managed by a smart pointer.
     * 
     * Forwards the provided arguments directly to the target object's constructor.
     * 
     * @tparam T The type of the object to create.
     * @tparam Args The types of the constructor arguments.
     * @param args The arguments passed to the object's constructor.
     * @return A new smart pointer managing the instantiated object.
     */
    template <typename T, typename... Args>
    sushi_ptr<T> make_sushi(Args&&... args) 
    {
        return sushi_ptr<T>(new T(std::forward<Args>(args)...));
    }
    
} // namespace SushiRuntime

namespace std 
{
    /**
     * @brief Provides a hashing mechanism for the smart pointer.
     * 
     * Hashes the underlying raw pointer to support usage in standard unordered containers.
     * 
     * @tparam T The type of the managed object.
     */
    template <typename T>
    struct hash<SushiRuntime::sushi_ptr<T>> 
    {
        std::size_t operator()(const SushiRuntime::sushi_ptr<T>& sp) const noexcept 
        {
            return std::hash<T*>()(sp.get());
        }
    };
}