# src/CMakeLists.txt

# Kaynak dosyalarını belirle
set(SB_SRC_FILES
    "core/ref_counted.h"
    "core/storage.h"
    "core/sushi_ptr.h"
    "core/tensor.h"
    "device/cpu_device.cpp"
    "device/cuda_device.cu"
    "device/device.h"
    "device/device_manager.cpp"
    "event/cuda_event.h"
    "event/event.h"
    "memory/memory_block.cpp"
    "memory/memory_block.h"
    "memory/memory_pool.h"
    "stream/cpu_stream.h"
    "stream/cuda_stream.h"
    "stream/stream.h"
)

# SushiBLAS_src kütüphanesini oluştur
add_library(SushiBLAS_src STATIC ${SB_SRC_FILES})

# Include dizinlerini ayarla
target_include_directories(SushiBLAS_src PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/device
    ${CMAKE_CURRENT_SOURCE_DIR}/event
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/stream
)

# CUDA ayarlarını yapılandır
if(CMAKE_CUDA_COMPILER)
    set_target_properties(SushiBLAS_src PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
    )
    
    # MSVC ve CUDA uyumluluğu için
    if(MSVC)
        target_compile_options(SushiBLAS_src PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math -O3>
        )
    endif()
endif()

# OpenMP desteği
if(OpenMP_CXX_FOUND)
    target_link_libraries(SushiBLAS_src PUBLIC OpenMP::OpenMP_CXX)
endif()
