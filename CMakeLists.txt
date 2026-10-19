cmake_minimum_required(VERSION 3.18)
project(codm_tex)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(DEBUG_SYMBOLS_PATH ${CMAKE_BINARY_DIR}/symbols)

# LSPlt (PLT hook library) is cloned next to this file by the CI workflow.
add_subdirectory(LSPlt/lsplt/src/main/jni lsplt)

# Zygisk module library; packaged as zygisk/<abi>.so.
add_library(codm_tex SHARED main.cpp)
target_include_directories(codm_tex PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/LSPlt/lsplt/src/main/jni/include)
target_link_libraries(codm_tex lsplt_static log android)

# v0.40: 16 KB page-size phones (Android 15+) need segments aligned to 16 KB. NDK r28+ does this by default; older
# NDKs on the runner need the flag. Harmless on r28+.
target_link_options(codm_tex PRIVATE -Wl,-z,max-page-size=16384)
