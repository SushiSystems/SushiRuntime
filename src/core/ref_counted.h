#pragma once

#include <atomic>
#include <iostream>

namespace SushiBLAS 
{
    class RefCounted 
    {
        protected:
            mutable std::atomic<int32_t> ref_count{0};

            virtual ~RefCounted() = default;

        public:
            void add_ref() const noexcept 
            {
                ref_count.fetch_add(1, std::memory_order_relaxed);
            }

            void release() const noexcept 
            {
                if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) 
                {
                    delete this; 
                }
            }

            int32_t get_ref_count() const noexcept 
            {
                return ref_count.load(std::memory_order_relaxed);
            }
    };
    
} // namespace SushiBLAS