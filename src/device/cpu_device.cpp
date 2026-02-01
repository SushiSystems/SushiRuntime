#include <cstdlib>
#include <cstring>

#include "cpu_stream.h"
#include "device.h"

namespace SushiBLAS 
{
    class CPUEvent final : public Event 
    {
        bool completed = false;
        const char* name;

        public:
            CPUEvent(const char* n) : name(n) {}
            void record(Stream* s) override { completed = true; }
            bool is_completed() const override { return completed; }

            void synchronize() override { /* Already sync on CPU */ }

            const char* get_name() const override { return name; }
            float elapsed_time(Event* start) override { return 0.0f; }
    };

    class CPUDevice final : public Device 
    {
        sushi_ptr<Stream> default_stream;

        public:
            CPUDevice() : Device(-1) 
            {
                default_stream = create_stream();
            }

            // TODO: Print the actual CPU name
            const char* get_name() const override { return "Host CPU"; } 
            int get_id() const override { return -1; }

            sushi_ptr<Stream> create_stream() override { return make_sushi<CPUStream>(); }

            sushi_ptr<Stream> get_default_stream() override { return default_stream; }

            sushi_ptr<Event> create_event(const char* name) override { return make_sushi<CPUEvent>(name); }

            void synchronize() override { /* Already sync on CPU */ }

            void copy_h2d(void* dst, const void* src, size_t size, Stream* s) override { std::memcpy(dst, src, size); }
            void copy_d2h(void* dst, const void* src, size_t size, Stream* s) override { std::memcpy(dst, src, size); }
            void copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* s) override { std::memcpy(dst, src, size); }

        protected:
            void* allocate_raw(size_t size) override 
            {
                #if defined(_MSC_VER) || defined(__MINGW32__)
                    return _aligned_malloc(size, 64);
                #else
                    void* ptr = nullptr;
                    if (posix_memalign(&ptr, 64, size) != 0) return nullptr;
                    return ptr;
                #endif
            }

            void free_raw(void* ptr) override 
            {
                #if defined(_MSC_VER) || defined(__MINGW32__)
                    _aligned_free(ptr);
                #else
                    std::free(ptr);
                #endif
            }
    };
} // namespace SushiBLAS