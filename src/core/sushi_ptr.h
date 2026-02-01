#pragma once

#include "ref_counted.h"

namespace SushiBLAS 
{
    template <typename T>
    class sushi_ptr 
    {
        private:
            T* ptr = nullptr;

            template <typename U>
            friend class sushi_ptr;
            
        public:
            sushi_ptr() : ptr(nullptr) { }

            sushi_ptr(T* p) : ptr(p) { if (ptr) ptr->add_ref(); }
                
            sushi_ptr(const sushi_ptr& other) : ptr(other.ptr) { if (ptr) ptr->add_ref(); }

            template <typename U>
            sushi_ptr(const sushi_ptr<U>& other) : ptr(other.ptr) { if (ptr) ptr->add_ref(); }

            sushi_ptr(sushi_ptr&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }

            ~sushi_ptr() { if (ptr) ptr->release(); }

            sushi_ptr& operator=(const sushi_ptr& other) 
            {
                if (this != &other) 
                {
                    if (ptr) ptr->release();
                    ptr = other.ptr;
                    if (ptr) ptr->add_ref();
                }
                return *this;
            }

            template <typename U>
            sushi_ptr& operator=(const sushi_ptr<U>& other) 
            {
                if (reinterpret_cast<void*>(this) != reinterpret_cast<void*>(&other)) 
                {
                    if (ptr) ptr->release();
                    ptr = other.ptr;
                    if (ptr) ptr->add_ref();
                }
                return *this;
            }

            T* operator->() const { return ptr; }
            T& operator*() const { return *ptr; }
            T* get() const { return ptr; }
            explicit operator bool() const { return ptr != nullptr; }
    };

    template <typename T, typename... Args>
    sushi_ptr<T> make_sushi(Args&&... args) 
    {
        return sushi_ptr<T>(new T(std::forward<Args>(args)...));
    }

} // namespace SushiBLAS