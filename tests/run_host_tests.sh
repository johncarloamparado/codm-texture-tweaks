#!/bin/sh
# Host-side tests (Linux, g++). Not an Android build. Usage: tests/run_host_tests.sh  (from the repo root, or anywhere)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${TMPDIR:-/tmp}/codm_ht_$$"
mkdir -p "$W/files" && chmod 777 "$W/files"
sed "s#\"/data/data/\" PKG \"/files/\"#\"$W/files/\"#" "$ROOT/main.cpp" > "$W/main_test_copy.cpp"
for t in cfg scale chain v040; do
  g++ -std=c++20 -w -I"$ROOT/tests/stubs" -I"$W" "$ROOT/tests/host_test_$t.cpp" -o "$W/t_$t" -Wl,--unresolved-symbols=ignore-all -lpthread -ldl
  echo "== $t"; (cd "$W" && "./t_$t")
done
rm -rf "$W"
