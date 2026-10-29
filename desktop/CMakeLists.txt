# VADER desktop: the synth as a VST3 and a VST2 plugin for Windows and Linux (DPF + NanoVG).
#
#   Linux:    cmake -S desktop -B build/desktop -DCMAKE_BUILD_TYPE=Release && cmake --build build/desktop -j
#   Windows:  the same with a MinGW-w64 toolchain (see docs/DESKTOP.md and .github/workflows/desktop.yml)
#
# The engine is the Force build's own src/engine.c (the same sound), compiled as a C library; desktop/view_bus.c stands in
# for src/overlay.c and src/web.c (the screen is drawn by the plugin's window), and tools/gen_desktop.py generates the
# parameter list and the pages (desktop/gen/) from the same sources the Force build uses.
cmake_minimum_required(VERSION 3.15)
project(VADER C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

get_filename_component(VADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DPF_ROOT "${VADER_ROOT}/third_party/DPF")
add_subdirectory("${DPF_ROOT}" dpf)

# ---- the engine (C)
set(ENGINE_SRCS
  ${VADER_ROOT}/src/tables.c ${VADER_ROOT}/src/shape.c ${VADER_ROOT}/src/paths.c ${VADER_ROOT}/src/shapefile.c ${VADER_ROOT}/src/screen.c
  ${VADER_ROOT}/src/fx.c ${VADER_ROOT}/src/pictures.c ${VADER_ROOT}/src/qr.c ${VADER_ROOT}/src/engine.c
  ${CMAKE_CURRENT_SOURCE_DIR}/view_bus.c)
add_library(vader_engine STATIC ${ENGINE_SRCS})
target_include_directories(vader_engine PUBLIC
  ${VADER_ROOT}/src ${VADER_ROOT}/third_party/stb ${VADER_ROOT}/third_party/mpc-vst-plugins/wrapper)
target_compile_definitions(vader_engine PRIVATE VADER_DESKTOP=1 _GNU_SOURCE=1)
if(WIN32)
  target_compile_options(vader_engine PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/compat.h)
  target_link_libraries(vader_engine PUBLIC winpthread)
else()
  target_link_libraries(vader_engine PUBLIC pthread m)
endif()
target_compile_options(vader_engine PRIVATE -w -O2 -ffp-contract=off)

# ---- the plugin
dpf_add_plugin(VADER
  TARGETS vst2 vst3
  UI_TYPE opengl
  FILES_COMMON
    gen/vader_res.cpp
  FILES_DSP
    VaderPlugin.cpp
    paths.cpp
  FILES_UI
    VaderUI.cpp)

target_include_directories(VADER PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(VADER PUBLIC vader_engine)
target_include_directories(VADER-ui PRIVATE ${DPF_ROOT}/dgl/src)
