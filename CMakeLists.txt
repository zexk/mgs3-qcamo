cmake_minimum_required(VERSION 3.20)
project(qcamo LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
set(IMGUI_DIR "$ENV{imgui}" CACHE PATH "Path to ImGui")
if(NOT IMGUI_DIR)
  FetchContent_Declare(imgui_source
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui_source)
  set(IMGUI_DIR "${imgui_source_SOURCE_DIR}")
endif()
set(MINHOOK_DIR "$ENV{minhook}" CACHE PATH "Path to MinHook")
if(NOT MINHOOK_DIR)
  FetchContent_Declare(minhook_source
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(minhook_source)
  set(MINHOOK_DIR "${minhook_source_SOURCE_DIR}")
endif()

add_library(minhook STATIC
  ${MINHOOK_DIR}/src/buffer.c ${MINHOOK_DIR}/src/hook.c
  ${MINHOOK_DIR}/src/trampoline.c ${MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)
target_include_directories(minhook PRIVATE ${MINHOOK_DIR}/src ${MINHOOK_DIR}/src/hde)

add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

add_library(qcamo SHARED src/qcamo.cpp src/overlay.cpp src/camo_swatch.cpp
  src/ctxr.cpp src/hud_font.cpp src/common/log.cpp)
set_target_properties(qcamo PROPERTIES PREFIX "" SUFFIX ".asi")
target_include_directories(qcamo PRIVATE src)
target_link_libraries(qcamo PRIVATE minhook imgui d3d11 dxgi d3dcompiler dwmapi)

if(MINGW)
  target_link_options(qcamo PRIVATE -static -static-libgcc -static-libstdc++)
endif()
