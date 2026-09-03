cmake_minimum_required(VERSION 3.26)
project(ResourceBroker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RESOURCEBROKER_BUILD_TESTS "Build Resource Broker tests" ON)
option(RESOURCEBROKER_BUILD_EXAMPLES "Build Resource Broker examples" ON)
option(RESOURCEBROKER_BUILD_BENCHMARKS "Build Resource Broker benchmarks" OFF)
option(RESOURCEBROKER_ENABLE_SYNTHETIC_RESOURCES "Enable synthetic resource pools" ON)
option(RESOURCEBROKER_ENABLE_CUDA_PROOF "Enable real CUDA (RTX 5090) resource proofs" OFF)

if(RESOURCEBROKER_ENABLE_CUDA_PROOF)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Source files for the core library.
file(GLOB RB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(resourcebroker ${RB_SOURCES})
add_library(ResourceBroker::resourcebroker ALIAS resourcebroker)
set_target_properties(resourcebroker PROPERTIES
    EXPORT_NAME resourcebroker
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(resourcebroker PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(resourcebroker PRIVATE /W4 /WX /EHsc /permissive- /Zc:__cplusplus)
    target_compile_definitions(resourcebroker PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(resourcebroker PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(RESOURCEBROKER_ENABLE_CUDA_PROOF)
    find_package(CUDAToolkit REQUIRED)
    target_link_libraries(resourcebroker PUBLIC CUDA::cudart)
    target_compile_definitions(resourcebroker PUBLIC RESOURCEBROKER_HAS_CUDA=1)
    if(MSVC)
        target_compile_options(resourcebroker PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4;Xcompiler=/WX>)
    endif()
endif()

install(TARGETS resourcebroker EXPORT ResourceBrokerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/resourcebroker DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(RESOURCEBROKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(RESOURCEBROKER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(RESOURCEBROKER_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
add_subdirectory(tools)
