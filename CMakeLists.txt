cmake_minimum_required(VERSION 3.18)

# CUDA path
set(CMAKE_CUDA_COMPILER "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/bin/nvcc.exe")

project(SushiBLAS LANGUAGES CXX CUDA)

find_package(OpenMP COMPONENTS CXX REQUIRED)

# GTX 1060 (Pascal) mimarisi
set(CMAKE_CUDA_ARCHITECTURES 61)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# root
set(SB_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(SB_API_DIR "${SB_ROOT_DIR}/api")         # root/api
set(SB_SRC_DIR "${SB_ROOT_DIR}/src")         # root/src
set(SB_CORE_DIR "${SB_SRC_DIR}/core")       # root/src/core

set(SB_INC_DIRS 
    ${SB_API_DIR}
    ${SB_SRC_DIR}
)

set(SB_CPP_SRC
    "${SB_ROOT_DIR}/main.cpp" 
)

add_subdirectory(src)


# error handling
foreach(file ${SB_CPP_SRC})
    if(NOT EXISTS ${file})
        message(FATAL_ERROR "File not found: ${file}")
    endif()
endforeach()

add_executable(SushiBLAS_prototype ${SB_CPP_SRC})

target_include_directories(SushiBLAS_prototype PRIVATE ${SB_INC_DIRS})

target_link_libraries(SushiBLAS_prototype PRIVATE SushiBLAS_src)

if(OpenMP_CXX_FOUND)
    target_link_libraries(SushiBLAS_prototype PUBLIC OpenMP::OpenMP_CXX)
endif()

# MSVC
if(MSVC)
    target_compile_options(SushiBLAS_prototype PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:
            /arch:AVX2
            /O2
            /Oi
            /Ot
            /EHsc
        >
    )

    # OpenMP sadece link-time
    target_link_libraries(SushiBLAS_prototype PRIVATE OpenMP::OpenMP_CXX)

    set_target_properties(SushiBLAS_prototype PROPERTIES
        CUDA_PROPAGATE_HOST_FLAGS OFF
    )

    target_compile_options(SushiBLAS_prototype PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math -O3>
    )
endif()

# MSVC Ayarları
#if(MSVC)
    # 1. Tüm C++ dosyaları için temel bayraklar
    #target_compile_options(SushiTest PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/arch:AVX2 /openmp /W4 /EHsc>)

    # 2. Sadece Release modunda hız optimizasyonlarını ekle
    #target_compile_options(SushiTest PRIVATE $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2 /Oi /Ot>)
    
    # CUDA dosyaları için temiz bayraklar (MSVC bayraklarını sızdırma)
    #set_target_properties(SushiTest PROPERTIES CUDA_PROPAGATE_HOST_FLAGS OFF)
    
    # CUDA tarafına sadece gerekli compiler flaglarını manuel gönderelim
    #target_compile_options(SushiTest PUBLIC $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
#endif()