#pragma once

#include <iostream>

#include "../device/device.h"
#include "stream.h"

namespace SushiBLAS 
{
    class CPUStream final : public Stream 
    {
        public:
            CPUStream() = default;

            // since everything runs synchronously on CPU (for now), these methods are empty.
            void synchronize() override { /* Sequential execution, no-op */ }
            bool is_completed() override { return true; }
            
            // on CPU, the native handle can be a thread ID or nullptr.
            void* get_native_handle() const override { return nullptr; }

            void wait(Event* event) override 
            {
                if (event) event->synchronize();
            }

            const char* get_name() const override { return "CPUStream"; }
            int64_t get_id() const override { return 0; }
            DeviceType get_device_type() const override { return DeviceType::CPU; }
    };
}