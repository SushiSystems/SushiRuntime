#pragma once

#include "device.h"

namespace SushiBLAS 
{
    class CPUDevice final : public Device 
    {
        private:
            sushi_ptr<Stream> default_stream_;
            char device_name_[128];

        public:
            CPUDevice();
            ~CPUDevice() override;

            const char* get_name() const override;
            int get_id() const override;
            sushi_ptr<Stream> create_stream() override;
            sushi_ptr<Stream> get_default_stream() override;
            sushi_ptr<Event> create_event(const char* name) override;
            void synchronize() override;

            void copy_h2d(void* dst, const void* src, size_t size, Stream* s) override;
            void copy_d2h(void* dst, const void* src, size_t size, Stream* s) override;
            void copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* s) override;

        protected:
            void* allocate_raw(size_t size) override;
            void free_raw(void* ptr) override;
    };
}