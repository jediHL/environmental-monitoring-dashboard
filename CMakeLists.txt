cmake_minimum_required(VERSION 3.25)

project(environmental_monitoring_dashboard LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(raylib)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        cjson
        URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cjson)

add_library(data_source STATIC data_source.c)
target_include_directories(data_source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                           PRIVATE ${cjson_SOURCE_DIR})
target_link_libraries(data_source PRIVATE cjson)

if(WIN32)
    target_link_libraries(data_source PRIVATE winhttp)
else()
    find_package(CURL REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(data_source PRIVATE CURL::libcurl Threads::Threads)
endif()

add_executable(dashboard main.c)

target_link_libraries(dashboard PRIVATE raylib data_source)

# Package the font next to the executable for both CLion and Visual Studio.
add_custom_command(TARGET dashboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:dashboard>/assets"
    VERBATIM
)

if(MSVC)
    target_compile_options(dashboard PRIVATE /W4)
    target_compile_options(data_source PRIVATE /W4)
else()
    target_compile_options(dashboard PRIVATE -Wall -Wextra)
    target_compile_options(data_source PRIVATE -Wall -Wextra)
endif()
