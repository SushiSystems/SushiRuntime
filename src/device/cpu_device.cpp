#include "cpu_device.h"
#include "../stream/cpu_stream.h"
#include "../event/cpu_event.h" // CPUEvent tanımı için gerekli
#include <cstdlib>
#include <cstring>

namespace SushiBLAS 
{
    CPUDevice::CPUDevice() : Device(-1) 
    {
        std::strncpy(device_name_, "Host CPU", sizeof(device_name_));
        default_stream_ = create_stream();
    }

    CPUDevice::~CPUDevice() 
    {
        synchronize(); // Önceki aşamada eklediğimiz gibi
    }

    const char* CPUDevice::get_name() const 
    { 
        return device_name_; 
    }

    int CPUDevice::get_id() const 
    { 
        return -1; 
    }

    sushi_ptr<Stream> CPUDevice::create_stream() 
    { 
        return make_sushi<CPUStream>(); 
    }

    sushi_ptr<Stream> CPUDevice::get_default_stream() 
    { 
        return default_stream_; 
    }

    sushi_ptr<Event> CPUDevice::create_event(const char* name) 
    { 
        return make_sushi<CPUEvent>(name); 
    }

    void CPUDevice::synchronize() 
    { 
        // CPU üzerinde işlemler zaten senkron ilerler.
    }

    void CPUDevice::copy_h2d(void* dst, const void* src, size_t size, Stream* s) 
    { 
        std::memcpy(dst, src, size); 
    }

    void CPUDevice::copy_d2h(void* dst, const void* src, size_t size, Stream* s) 
    { 
        std::memcpy(dst, src, size); 
    }

    void CPUDevice::copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* s) 
    { 
        std::memcpy(dst, src, size); 
    }

    void* CPUDevice::allocate_raw(size_t size) 
    {
        #if defined(_MSC_VER) || defined(__MINGW32__)
            return _aligned_malloc(size, CACHE_LINE_SIZE);
        #else
            void* ptr = nullptr;
            if (posix_memalign(&ptr, CACHE_LINE_SIZE, size) != 0) return nullptr;
            return ptr;
        #endif
    }

    void CPUDevice::free_raw(void* ptr) 
    {
        #if defined(_MSC_VER) || defined(__MINGW32__)
            _aligned_free(ptr);
        #else
            std::free(ptr);
        #endif
    }
} // namespace SushiBLAS