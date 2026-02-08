#pragma once

#include <cstdint>

#include "../core/ref_counted.h"

namespace SushiBLAS 
{
    enum class DeviceType; 
    class Event;
    
    class Stream : public RefCounted 
    {
        public:
            virtual ~Stream() = default;

            virtual void wait(Event* event) = 0;

            virtual void synchronize() = 0;
            virtual bool is_completed() = 0;

            virtual void* get_native_handle() const = 0;

            virtual int64_t get_id() const = 0;
            virtual const char* get_name() const = 0;
            
            virtual DeviceType get_device_type() const = 0;

        protected:
            Stream() = default;
    };
} // namespace SushiBLAS