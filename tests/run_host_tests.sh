#!/bin/sh
# Host-side tests (Linux, g++). Not an Android build. Usage: tests/run_host_tests.sh  (from the repo root, or anywhere)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${TMPDIR:-/tmp}/codm_ht_$$"
# v0.44.1 lint: a thread_local with a destructor aborts inside the Zygisk-loaded module on a real phone (host tests cannot see it).
if grep -nE 'thread_local[^;=]*(std::|string|vector|map|unique_ptr|shared_ptr)' "$ROOT/main.cpp" | grep -v '^[0-9]*:[[:space:]]*/\*'; then
  echo "FAIL: non-trivial thread_local in main.cpp (see lines above)"; exit 1
fi
echo "== lint ok (no non-trivial thread_local)"
mkdir -p "$W/files" && chmod 777 "$W/files"
sed "s#\"/data/data/\" PKG \"/files/\"#\"$W/files/\"#" "$ROOT/main.cpp" > "$W/main_test_copy.cpp"
for t in cfg scale chain v040 v041 v044; do
  g++ -std=c++20 -w -I"$ROOT/tests/stubs" -I"$W" "$ROOT/tests/host_test_$t.cpp" -o "$W/t_$t" -Wl,--unresolved-symbols=ignore-all -lpthread -ldl
  echo "== $t"; (cd "$W" && "./t_$t")
done
rm -rf "$W"
