#pragma once

#include <iostream>

#include "../device/device.h"
#include "../event/cpu_event.h"
#include "../common/logging/logger.h"
#include "stream.h"

namespace SushiBLAS 
{
    /**
     * @brief CPUStream: Host tarafında sıralı (sequential) işlem yürütür.
     * * @note DAVRANIŞ GARANTİSİ:
     * 1. Bu stream üzerindeki tüm işlemler (synchronize, wait) BLOKLAYICIDIR.
     * 2. Asenkron işlem (future/thread) desteklemez. Çağrıldığı thread'de çalışır.
     * 3. Cross-device senkronizasyon için önce ilgili cihazın işini bitirmesi beklenmelidir.
     */
    class CPUStream final : public Stream 
    {
        public:
            CPUStream() = default;

            // CPU işlemleri senkron olduğu için, bu fonksiyon çağrıldığı an zaten her şey bitmiştir.
            void synchronize() override 
            { 
                // No-op (Zaten senkron)
            }

            // CPU işlemleri kuyruğa atılmadığı (hemen yapıldığı) için her zaman completed döner.
            bool is_completed() override 
            { 
                return true; 
            }
            
            // CPU Stream'in native bir handle'ı yoktur (Thread ID belki, ama şimdilik nullptr).
            void* get_native_handle() const override { return nullptr; }

            void wait(Event* event) override 
            {
                SB_THROW_IF(event == nullptr, "CPUStream::wait: Event cannot be null");

                // STRICT TYPE CHECK:
                // CPU Stream sadece CPU eventlerini bekleyebilir.
                // Eğer bir CUDA Event beklenmek isteniyorsa, önce o CUDA event'in 
                // GPU tarafında bitmiş olması (synchronize edilmesi) gerekir.
                
                CPUEvent* cpu_ev = dynamic_cast<CPUEvent*>(event);
                if (!cpu_ev)
                {
                    SB_LOG_ERROR("CPUStream: Device Mismatch! CPUStream cannot wait on a non-CPU event directly.");
                    SB_LOG_ERROR("Hint: Call event->synchronize() manually before strictly ordering on CPU.");
                    throw std::runtime_error("CPUStream Device Mismatch");
                }

                // Senkronizasyon noktası:
                // CPUEvent kaydedilene (record) kadar buradaki thread bloklanır.
                cpu_ev->synchronize();
            }

            const char* get_name() const override { return "CPUStream"; }
            int64_t get_id() const override { return 0; }
            DeviceType get_device_type() const override { return DeviceType::CPU; }
    };
}