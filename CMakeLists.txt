cmake_minimum_required(VERSION 3.15)

project(delete_net LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "delete_net can only be built for Windows")
endif()

add_executable(remove-net-device
    remove_net_device.cpp
)

target_compile_features(remove-net-device PRIVATE cxx_std_14)
target_link_libraries(remove-net-device PRIVATE setupapi newdev)

set_target_properties(remove-net-device PROPERTIES
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(remove-net-device PRIVATE /W4)
    set_property(
        TARGET remove-net-device
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
else()
    target_compile_options(remove-net-device PRIVATE -Wall -Wextra -Wpedantic)
endif()
