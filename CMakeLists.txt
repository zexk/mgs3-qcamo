cmake_minimum_required(VERSION 3.20)
project(qcamo LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
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

add_library(qcamo SHARED src/qcamo.cpp src/common/log.cpp)
set_target_properties(qcamo PROPERTIES PREFIX "" SUFFIX ".asi")
target_include_directories(qcamo PRIVATE src)
target_link_libraries(qcamo PRIVATE minhook)

if(MINGW)
  target_link_options(qcamo PRIVATE -static -static-libgcc -static-libstdc++)
endif()
