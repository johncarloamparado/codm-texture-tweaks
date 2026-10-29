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
# v0.57 lint: range rules and the old 60 / 64 rule limits must not come back
if grep -nE 'GrRange|g_range\b|GRL_RANGE_MATCH|GRASS_MAX_RULES[[:space:]]+(60|64)\b' "$ROOT/main.cpp" | grep -v 'g_grass_range_old' | grep -vE '^[0-9]+:[[:space:]]*(/\*|//)'; then
  echo "FAIL: range rule code or an old rule limit is back in main.cpp"; exit 1
fi
if grep -nE '\b(gRange|baseRange|rangeLine|rangeMembers|orphanHtml|gKeepRange|gPendLine)\b|(length|\.n) *>=? *(60|64)\b' "$ROOT/webroot/index.html"; then
  echo "FAIL: range rule code or an old 60 / 64 rule limit is back in webroot/index.html"; exit 1
fi
echo "== lint ok (no range rules, no 60/64 rule limit)"
# v0.65.1 lint: the Search setup state (sf, sfOpen) must be declared; a missing declaration made render() throw inside startForm()
# (swallowed by .then(bootDone, bootDone)) so the Texture browser / finder never rendered.
for v in sf sfOpen PENCIL CROSS GRIP TRASH libArm libArmT gSetOpen; do
  if ! grep -qE "^[[:space:]]*var[[:space:]]+$v[[:space:]]*=" "$ROOT/webroot/index.html"; then
    echo "FAIL: 'var $v' is not declared in webroot/index.html"; exit 1
  fi
done
# v0.65.1 lint: every sheet / window (tbOv, twOv, picker) must be shown with the "on" class, or it stays at opacity 0
if [ "$(grep -c 'requestAnimationFrame(function () { ov.classList.add("on")' "$ROOT/webroot/index.html")" -lt 3 ]; then
  echo "FAIL: a sheet / window no longer gets the 'on' class (test window would be invisible)"; exit 1
fi
echo "== lint ok (sf / sfOpen / PENCIL / CROSS / GRIP / TRASH / libArm / libArmT / gSetOpen declared)"
mkdir -p "$W/files" && chmod 777 "$W/files"
sed "s#\"/data/data/\" PKG \"/files/\"#\"$W/files/\"#" "$ROOT/main.cpp" > "$W/main_test_copy.cpp"
for t in cfg scale chain v040 v041 v044 v045 v057 v061 v065; do
  g++ -std=c++20 -w -I"$ROOT/tests/stubs" -I"$W" "$ROOT/tests/host_test_$t.cpp" -o "$W/t_$t" -Wl,--unresolved-symbols=ignore-all -no-pie -lpthread -ldl
  echo "== $t"; (cd "$W" && "./t_$t")
done
rm -rf "$W"
# WebUI checks (v0.57): JavaScript syntax, then the headless-browser tests (skipped when node / playwright are not installed)
if command -v node >/dev/null 2>&1; then
  echo "== js syntax"; node "$ROOT/tests/js_syntax_check.js"
  echo "== persistence regressions"; node "$ROOT/tests/persistence_test.js"
else
  echo "== js syntax SKIPPED (node not installed)"
  echo "== persistence regressions SKIPPED (node not installed)"
fi
if command -v python3 >/dev/null 2>&1; then echo "== ui_test"; python3 "$ROOT/tests/ui_test.py"; else echo "== ui_test SKIPPED (python3 not installed)"; fi
