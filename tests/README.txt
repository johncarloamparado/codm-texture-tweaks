Host-side tests (Linux, g++). NOT an Android build. Needs stub headers for zygisk.hpp, lsplt.hpp, jni.h,
EGL/egl.h, GLES3/gl3.h, android/{log,dlext,native_window}.h (declarations only).
Simplest way: ./tests/run_host_tests.sh   (uses tests/stubs/ for the Android / Zygisk / LSPlt headers; CI runs it too).
Manual build: sed 's#"/data/data/" PKG "/files/"#"/tmp/ht/files/"#' main.cpp > main_test_copy.cpp; mkdir -p /tmp/ht/files; chmod 777 /tmp/ht/files;
  g++ -std=c++20 -w -Itests/stubs -I. -Wl,--unresolved-symbols=ignore-all tests/host_test_cfg.cpp -o t -lpthread -ldl && ./t
Tests: v044 (Keep alpha soft, Remove grass), cfg (settings audit, log), scale (render scale maths), chain (original-function safety net), v040 (run-time folder, live FPS, hook crash breaker).
Run as root and as a normal user (the log-recreate test needs non-root).
v057 (v0.57): grass.txt exact rules only, max 4096 unique rules (parser cap boundary 4095/4096/4097, duplicates, CRLF/comments/malformed lines,
  obsolete "range" lines ignored, sorted binary-search lookup, guard still wins). Replaces the old v047/v055 range tests.
ui_test.py (v0.57): WebUI in headless Chromium (demo mode and a mocked shell). Candidate list above 1500 (5003 and 4300 distinct, duplicates merged, chunks of 400),
  40 rows at a time, hash-prefix / see-through search, selection of more than 60 textures, test selected / test filtered / hide / show, 4096 accepted and 4097 refused
  without writing, side test above 60 with Back, old range lines + duplicates + legacy lines cleaned, old range based finder state dropped, exact state kept, saved names kept,
  atomic chunked write (no shell command over 60 KB). Needs `pip install playwright` and `playwright install chromium`; without them it prints SKIP.
js_syntax_check.js: syntax check of the inline scripts of webroot/index.html (needs only node).
