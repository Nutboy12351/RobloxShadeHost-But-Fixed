cmake_minimum_required(VERSION 3.20)
project(RobloxShadeHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(RobloxShadeHost src/main.cpp)
target_compile_definitions(RobloxShadeHost PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(RobloxShadeHost PRIVATE /W4 /permissive- /EHsc /bigobj)
target_link_libraries(RobloxShadeHost PRIVATE d3d11 dxgi dwmapi windowsapp)
