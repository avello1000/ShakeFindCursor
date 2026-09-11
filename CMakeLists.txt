# ShakeFindCursor — CMake 构建脚本（MSVC 专用）
#
# 用法（在仓库根目录）：
#   cmake -S . -B build -A x64
#   cmake --build build --config Release
# 产物：build/Release/ShakeFindCursor.exe（及 e2e-test.exe / marquee-test.exe）
#
# 日常开发用 build.bat 也完全可以 —— 两者产物一致；CMake 的价值是
# 支持 IDE/生成器选择（VS / Ninja with cl），并让 CI 有第二种独立构建路径互相印证。
cmake_minimum_required(VERSION 3.15)
project(ShakeFindCursor LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR
        "ShakeFindCursor 仅支持 MSVC 工具链（GDI+ / rc 资源 / SetSystemCursor）。"
        "请在 Windows 上使用 Visual Studio 生成器，例如：cmake -S . -B build -A x64")
endif()

# app.rc 里的 FileDescription 是 UTF-8 中文，必须显式告知 rc.exe 代码页（同 build.bat）。
set_source_files_properties(app.rc PROPERTIES COMPILE_FLAGS "/c 65001")

# ---- 主程序 ----
add_executable(ShakeFindCursor main.cpp app.rc)
target_compile_options(ShakeFindCursor PRIVATE /W4 /EHsc /utf-8)
target_compile_definitions(ShakeFindCursor PRIVATE UNICODE _UNICODE)
target_link_options(ShakeFindCursor PRIVATE
    /SUBSYSTEM:WINDOWS
    /PDBALTPATH:ShakeFindCursor.pdb
    "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
target_link_libraries(ShakeFindCursor PRIVATE user32 gdi32 shell32 advapi32 gdiplus)

# ---- 验证工具（控制台程序）----
add_executable(e2e-test e2e-test.cpp)
target_compile_options(e2e-test PRIVATE /W4 /EHsc /utf-8)
target_compile_definitions(e2e-test PRIVATE UNICODE _UNICODE)
target_link_libraries(e2e-test PRIVATE user32 gdi32 shell32)

add_executable(marquee-test marquee-test.cpp)
target_compile_options(marquee-test PRIVATE /W4 /EHsc /utf-8)
target_compile_definitions(marquee-test PRIVATE UNICODE _UNICODE)
target_link_libraries(marquee-test PRIVATE user32 gdi32 gdiplus)
