cmake_minimum_required(VERSION 3.24)
project(heic_wic VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg provides libheif and libde265
find_package(libheif CONFIG REQUIRED)

# Our WIC codec DLL — include .def file in sources (CMake handles it for MSVC)
add_library(heic_wic SHARED
    src/dll_main.cpp
    src/class_factory.cpp
    src/codec.cpp
    src/frame_decode.cpp
    src/metadata_reader.cpp
    src/registration.cpp
    src/heic_wic.def
    src/version.rc
)

target_include_directories(heic_wic PRIVATE src)
# Note: vcpkg target may be heif::heif — check find_package output and adjust
target_link_libraries(heic_wic PRIVATE heif ole32 shlwapi windowscodecs advapi32)
set_target_properties(heic_wic PROPERTIES OUTPUT_NAME "heic_wic")

# Copy dependency DLLs next to our DLL for packaging
add_custom_command(TARGET heic_wic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        $<TARGET_FILE_DIR:heic_wic>
)

# Test executables
add_executable(test_decode test/test_decode.cpp)
target_link_libraries(test_decode PRIVATE ole32 shlwapi windowscodecs)

add_executable(test_wic_discovery test/test_wic_discovery.cpp)
target_link_libraries(test_wic_discovery PRIVATE ole32 shlwapi windowscodecs)

# Copy test HEIC sample to build dir at build time
add_custom_command(TARGET test_decode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test/samples
        ${CMAKE_BINARY_DIR}/test/samples
)
