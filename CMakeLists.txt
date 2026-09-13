cmake_minimum_required(VERSION 3.20)
project(RobloxShadeHost VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(RobloxShadeHost src/main.cpp)
target_compile_definitions(RobloxShadeHost PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(RobloxShadeHost PRIVATE ROBLOX_SHADE_HOST_VERSION="${PROJECT_VERSION}")
target_compile_options(RobloxShadeHost PRIVATE /W4 /permissive- /EHsc /bigobj)
target_link_libraries(RobloxShadeHost PRIVATE d3d11 dxgi dwmapi windowsapp gdi32)

find_program(INNO_SETUP_COMPILER ISCC
    HINTS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6" "C:/Program Files (x86)/Inno Setup 6")
if(INNO_SETUP_COMPILER)
    add_custom_target(installer
        COMMAND "${INNO_SETUP_COMPILER}" /Q
            "/DHostExe=$<TARGET_FILE:RobloxShadeHost>"
            "/DAppVersion=${PROJECT_VERSION}"
            "/O${CMAKE_BINARY_DIR}/installer"
            "${CMAKE_CURRENT_SOURCE_DIR}/installer/RobloxShadeHost.iss"
        DEPENDS RobloxShadeHost
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(hotkey_tests tests/hotkey_tests.cpp)
    target_compile_definitions(hotkey_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(hotkey_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME hotkey_tests COMMAND hotkey_tests)
endif()
