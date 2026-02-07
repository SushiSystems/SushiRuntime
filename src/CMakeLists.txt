# src/CMakeLists.txt

# AŞAMA 1.3 FIX: Explicit Source Listing (No GLOB)
set(SB_SRC_FILES
    # Common
    "common/logging/logger.h"
    "common/common.h"
    
    # Core
    "core/tensor.h"
    "core/sushi_ptr.h"
    "core/storage.h"
    "core/ref_counted.h"
    
    # Device
    "device/device.h"
    "device/device_manager.h"
    "device/device_manager.cpp"
    "device/cpu_device.h"
    "device/cpu_device.cpp"
    
    # Event
    "event/event.h"
    "event/cpu_event.h"
    
    # Memory
    "memory/memory_pool.h"
    "memory/memory_block.h"
    "memory/memory_block.cpp"
    
    # Stream
    "stream/stream.h"
    "stream/cpu_stream.h"
)

# CUDA Sources (Conditional)
if(CMAKE_CUDA_COMPILER)
    list(APPEND SB_SRC_FILES
        "device/cuda_device.h"
        "device/cuda_device.cu"
        "event/cuda_event.h"
        "stream/cuda_stream.h"
    )
endif()

# Library Definition
add_library(SushiBLAS_src STATIC ${SB_SRC_FILES})

if(CMAKE_CUDA_COMPILER)
    target_compile_definitions(SushiBLAS_src PUBLIC USE_CUDA)
endif()

# Include Directories
target_include_directories(SushiBLAS_src PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
