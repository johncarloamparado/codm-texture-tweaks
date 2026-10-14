Host-side tests (Linux, g++). NOT an Android build. Needs stub headers for zygisk.hpp, lsplt.hpp, jni.h,
EGL/egl.h, GLES3/gl3.h, android/{log,dlext,native_window}.h (declarations only).
1) sed 's#"/data/data/" PKG "/files/"#"/tmp/ht/files/"#' main.cpp > main_test_copy.cpp
2) mkdir -p /tmp/ht/files && chmod 777 /tmp/ht/files
3) g++ -std=c++20 -I<stubs> -I. -Wl,--unresolved-symbols=ignore-all tests/host_test_cfg.cpp -o t -lpthread && ./t
   (same for host_test_scale.cpp). Run once as root and once as a normal user (log-recreate test needs non-root).
