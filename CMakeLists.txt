cmake_minimum_required(VERSION 3.20)
project(nova-sight VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies via vcpkg (set CMAKE_TOOLCHAIN_FILE to vcpkg.cmake)
find_package(CURL CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(Leptonica CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(SOURCES
    src/main.cpp
    src/overlay.cpp
    src/capture.cpp
    src/ocr.cpp
    src/ai.cpp
    src/config.cpp
)

add_executable(nova-sight WIN32 ${SOURCES})

target_include_directories(nova-sight PRIVATE src)

target_link_libraries(nova-sight PRIVATE
    CURL::libcurl
    Tesseract::libtesseract
    nlohmann_json::nlohmann_json
    d2d1
    dwrite
    dwmapi
)

# Copy data/ to output directory after build
add_custom_command(TARGET nova-sight POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:nova-sight>/data
    COMMENT "Copying data directory to output"
)
