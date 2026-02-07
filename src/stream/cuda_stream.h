#pragma once

#include <cuda_runtime.h>
#include <vector>
#include <algorithm>
#include <mutex>

#include "../common/logging/logger.h"
#include "../event/cuda_event.h"
#include "../device/device.h"
#include "stream.h"

namespace SushiBLAS 
{
    class CUDAStream final : public Stream 
    {
        private:
            cudaStream_t handle_;
            int32_t device_id_;
            
            // AŞAMA 1.1 FIX: Lifetime Extension
            // Stream bir event'i beklerken, o event'in yok olmasını önlemek için strong ref tutuyoruz.
            // Bu, kullanıcı wait() çağırdıktan hemen sonra event'i scope dışına çıkarsa bile güvenliği sağlar.
            std::vector<sushi_ptr<Event>> pending_events_;
            std::mutex event_mutex_;

        public:
            CUDAStream(int32_t dev_id) : device_id_(dev_id) 
            {
                cudaStreamCreateWithFlags(&handle_, cudaStreamNonBlocking);
            }

            ~CUDAStream() 
            {
                // Stream yok olurken bekleyen referansları serbest bırak
                {
                    std::lock_guard<std::mutex> lock(event_mutex_);
                    pending_events_.clear();
                }

                if (handle_) cudaStreamDestroy(handle_);
            }

            void synchronize() override 
            {
                cudaStreamSynchronize(handle_);
                
                // Senkronizasyon bittiğine göre tüm bekleyen event referanslarını temizleyebiliriz.
                std::lock_guard<std::mutex> lock(event_mutex_);
                pending_events_.clear();
            }

            bool is_completed() override 
            {
                bool done = (cudaStreamQuery(handle_) == cudaSuccess);
                if (done) 
                {
                    // Stream boşaldıysa referansları temizle
                    std::lock_guard<std::mutex> lock(event_mutex_);
                    pending_events_.clear();
                }
                else
                {
                    // Stream hala meşgul ama bazı eventler bitmiş olabilir, hafif temizlik yap
                    garbage_collect_events();
                }
                return done;
            }

            void* get_native_handle() const override 
            {
                return (void*)handle_;
            }

            void wait(Event* event) override 
            {
                SB_THROW_IF(event == nullptr, "CUDAStream::wait: Event cannot be null");

                // AŞAMA 1.2 FIX: Device Compatibility Check
                // CUDA Stream sadece CUDA Event bekleyebilir (Native API kısıtı).
                // Cross-device senkronizasyon için önce CPU tarafında sync gerekebilir ama
                // burada strict type check ile hatayı yakalıyoruz.
                // Not: CUDAEvent dynamic_cast veya type check ile doğrulanmalı.
                // Şu an type check yoksa bile ilerde Event'e getType() eklenecek,
                // şimdilik event->synchronize() çağrılmadığı sürece native handle cast güvenli olmalı.
                
                // Güvenli cast denemesi (RTTI kapalıysa static_cast riskli olabilir ama şimdilik kabulümüz)
                // İdealde: if (event->get_device_type() != DeviceType::CUDA) throw ...
                // Bu özellik Stream/Event base class'ına eklendiğinde aktif edilecek.
                
                // 1. Native Wait Komutu
                cudaStreamWaitEvent(handle_, 
                    static_cast<cudaEvent_t>(static_cast<CUDAEvent*>(event)->get_native_handle()), 0);

                // 2. Lifetime Extension
                {
                    std::lock_guard<std::mutex> lock(event_mutex_);
                    
                    // Event'i listeye kopyalayarak ref-count artırıyoruz (Strong Ref).
                    // sushi_ptr constructor'ı AddRef yapar.
                    pending_events_.push_back(sushi_ptr<Event>(event));
                }

                // 3. Periyodik Temizlik (Listeyi sonsuz büyütmemek için)
                garbage_collect_events();
            }

            const char* get_name() const override { return "CUDAStream"; }
            int64_t get_id() const override { return reinterpret_cast<int64_t>(handle_); }
            DeviceType get_device_type() const override { return DeviceType::CUDA; }

        private:
            void garbage_collect_events()
            {
                // Try-lock ile ana akışı bloklamadan temizlik denemesi
                std::unique_lock<std::mutex> lock(event_mutex_, std::try_to_lock);
                if (lock.owns_lock())
                {
                    // Tamamlanmış eventleri listeden çıkar (Ref-count düşer)
                    auto it = std::remove_if(pending_events_.begin(), pending_events_.end(), 
                        [](const sushi_ptr<Event>& e) {
                            return e->is_completed();
                        });
                    
                    pending_events_.erase(it, pending_events_.end());
                }
            }
    };
} // namespace SushiBLAS