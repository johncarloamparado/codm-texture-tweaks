Host-side tests (Linux, g++). NOT an Android build. Needs stub headers for zygisk.hpp, lsplt.hpp, jni.h,
EGL/egl.h, GLES3/gl3.h, android/{log,dlext,native_window}.h (declarations only).
Simplest way: ./tests/run_host_tests.sh   (uses tests/stubs/ for the Android / Zygisk / LSPlt headers; CI runs it too).
Manual build: sed 's#"/data/data/" PKG "/files/"#"/tmp/ht/files/"#' main.cpp > main_test_copy.cpp; mkdir -p /tmp/ht/files; chmod 777 /tmp/ht/files;
  g++ -std=c++20 -w -Itests/stubs -I. -Wl,--unresolved-symbols=ignore-all tests/host_test_cfg.cpp -o t -lpthread -ldl && ./t
Tests: cfg (settings audit, log), scale (render scale maths), chain (original-function safety net), v040 (run-time folder, live FPS, hook crash breaker).
Run as root and as a normal user (the log-recreate test needs non-root).
