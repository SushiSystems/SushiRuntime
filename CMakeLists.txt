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
add_subdirectory(kernel)


# error handling
foreach(file ${SB_CPP_SRC})
    if(NOT EXISTS ${file})
        message(FATAL_ERROR "File not found: ${file}")
    endif()
endforeach()

add_executable(SushiBLAS_prototype ${SB_CPP_SRC})

target_include_directories(SushiBLAS_prototype PRIVATE ${SB_INC_DIRS})

target_link_libraries(SushiBLAS_prototype PRIVATE SushiBLAS_src SushiBLAS_kernels)

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

enable_testing() # Test mekanizmasını aktif et
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 6. Test Executable Tanımı
add_executable(sushiblas_tests
    "tests/main_test.cpp"
    "tests/test_memory_pool.cpp"
    "tests/test_device_manager.cpp"
    "tests/test_infrastructure_stress.cpp"
    "tests/test_cuda.cpp"
    "tests/test_tensor_stress.cpp"
    "tests/bench_infrastructure.cpp"
    "tests/test_sgemm.cpp"
)

# KRİTİK: Testleri doğrudan kütüphaneye (SushiBLAS_src) bağlayın
target_link_libraries(sushiblas_tests 
    PRIVATE 
    SushiBLAS_src 
    SushiBLAS_kernels
    GTest::gtest_main
)

target_include_directories(sushiblas_tests PRIVATE ${SB_INC_DIRS})

include(GoogleTest)
gtest_discover_tests(sushiblas_tests)

# MSVC Ayarları (Testler için de geçerli)
if(MSVC)
    target_compile_options(sushiblas_tests PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:
            /arch:AVX2
            /O2
            /Oi
            /Ot
            /EHsc
        >
    )
    # Linker: LIBCMT (Static CRT) çakışmasını engelle (Dynamic CRT /MD kullandığımız için)
    target_link_options(sushiblas_tests PRIVATE /NODEFAULTLIB:LIBCMT)
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