#pragma once

#include "device.h"

namespace SushiBLAS 
{
    class CPUDevice final : public Device 
    {
        private:
            sushi_ptr<Stream> default_stream_;
            HardwareInfo cached_info_;

        public:
            CPUDevice();
            ~CPUDevice() override;

            int get_id() const override;
            const char* get_name() const override;

            sushi_ptr<Stream> create_stream() override;
            sushi_ptr<Stream> get_default_stream() override;
            sushi_ptr<Event> create_event(const char* name) override;
            void synchronize() override;

            void copy_h2d(void* dst, const void* src, size_t size, Stream* s) override;
            void copy_d2h(void* dst, const void* src, size_t size, Stream* s) override;
            void copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* s) override;

            HardwareInfo get_hardware_info() const override;

        protected:
            void* allocate_raw(size_t size) override;
            void free_raw(void* ptr) override;
    };
} // namespace SushiBLAS