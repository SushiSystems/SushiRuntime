#pragma once

#include <cstdint>

#include "../core/ref_counted.h"

namespace SushiBLAS 
{
    class Stream;

    class Event : public RefCounted 
    {
        public:
            virtual ~Event() = default;

            virtual void record(Stream* stream) = 0;

            virtual bool is_completed() const = 0;

            virtual void synchronize() = 0;

            virtual const char* get_name() const = 0;
            virtual float elapsed_time(Event* start) = 0;

        protected:
            Event() = default;
    };
    
} // namespace SushiBLAS