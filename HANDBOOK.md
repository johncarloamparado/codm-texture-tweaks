# CODM Blocky Textures: project handbook

Zygisk module for Call of Duty: Mobile (`com.garena.game.codm`) with a WebUI. Module id `codm_tex_logger`.
Source layout: `main.cpp` (native module), `webroot/index.html` (WebUI), `module.prop`, `customize.sh`, `tests/`, `.github/workflows/build.yml`.

## Version policy (from v0.57)

| Kind of release | Version form | Example | `versionCode` |
|---|---|---|---|
| Major feature / behavior release | `v0.N` | `v0.57` | +1 |
| Minor release or bug-fix only | `v0.0.N` as written in the task, used here as `v0.N.P` (P counts the fixes made after `v0.N`) | `v0.57.1` | +1 |

- `v0.57` is a major feature release: exact-rules-only `grass.txt`, 4096 limit, selection based Texture Finder.
- `versionCode` always goes up by exactly 1 per release (`v0.57.4` = 72, `v0.57.3` = 71, `v0.57.2` = 70, `v0.57.1` = 69, `v0.57` = 68; the last release before it, `v0.56.0`, was 67).
- Note: the task text says bug-fix-only releases use `v0.0.N`. Read literally that would reset the number to `v0.0.1`, which sorts before every earlier release and would break the "newer version" checks in root managers. So the handbook uses `v0.N.P` (the next fix of `v0.57` is `v0.57.1`). Change this table if the literal form is really wanted.
- The version text must be changed together in: `VERSION` in `main.cpp`, `version` and `versionCode` in `module.prop`, the `echo 'UI: ...'` line in `webroot/index.html`, and the version checks in `tests/host_test_cfg.cpp` (it looks for the text `v0.57`).
- Zip name: `codm_Blocky_v<version>.zip`.
- Every release: update the demo `fake` version in `webroot/index.html` too, update this handbook and the handoff text, and hand over three files: the zip, `preview_index_v<version>.html` and the handoff txt.

## grass.txt (the list of hidden textures)

- One rule per line: `<format> <width> <height> <64-bit hex fingerprint>`, all four must match exactly.
- At most **4096 unique** rules (unique by format, width, height, fingerprint). The module keeps the first 4096 and logs the rest as ignored. The WebUI refuses a 4097th unique rule *before* anything is written.
- `range ...` lines (v0.47 to v0.56) do not exist any more. The module skips and counts them; the WebUI drops them the first time it rewrites the file and shows a one time notice.
- Lines are `#` comments, blank, or exact rules. Old two-field lines (`format size`, v0.44) and broken lines are ignored, never guessed.
- The module builds one sorted vector when the settings are loaded and uses a binary search in the upload hooks (no allocation, read-only after load).
- Guarded textures (scope / reticle atlas, lobby buttons) are never changed, whatever grass.txt says.

## WebUI behaviour that must not change by accident (v0.57.3)

- **A finder test is never ended by a switch.** In the test window (`#twOv`) a switch, `ON all`, `OFF all` and `Save` only *pause* the test (`gst.paused` / `gPend.paused`, kept in `ct_gst2` / `ct_gset`). While paused, grass.txt holds the person's own switches and may differ from the test list: `gLoadState` / `gLoadPend` skip the "grass.txt matches the test" check for a paused test. `Back to the test` (`gResume`, id `gfResume`) writes the test rules again. When the switches become exactly the test list again (ON all, or switching the last one back, `gSameSet`), the test runs again by itself (v0.57.4). `End the test` (`gEndTest`, id `gfEnd`) is the only way to end it on purpose, besides the old Cancel (`gfOff`). The answers (hidden / still there / back) are refused while paused.
- **My textures only holds what the person saved.** `gLoadLib` must never add rules from grass.txt as "Unnamed N" (during a test grass.txt holds hundreds of test rules). Rules that are hidden but not saved are listed in the Texture browser ("Hidden now, not saved"); `tbUnsaved()` leaves out the textures of a running, not paused test.
- **Delete in My textures.** Every row has `Delete` (`data-forget`, also turns the texture back to normal). Two bulk buttons (`data-libdel`, two taps, 5 s to confirm): `Delete unnamed that are OFF`, `Delete all that are OFF`. Bulk delete never touches entries that are ON.
- **Size filter = width OR height.** In the Texture browser and in the test window a size of 64 matches `64xN` and `Nx64` (`hasSide`, `sideCounts`). The number in the option list counts every texture that has that width or height. The finder search setup (smallest / largest texture to hide) still works on the longest side, on purpose (it is a range).

## Rules for the code


- No `thread_local` object with a destructor in `main.cpp` (the Zygisk loaded `.so` aborts). `tests/run_host_tests.sh` has a lint for it.
- Never read the data pointer of a texture upload while a pixel unpack buffer is bound.
- The WebUI writes files with `writeLinesAtomic` (pieces of 400 lines to `<file>.new`, line count check, then `mv`). One shell command must stay far below 128 KB.
- The texture list is read in chunks of 400 distinct `GRASSCAND` lines. Never cut it with `tail`.
- Class names `.sh`, `.sh-ov`, `.sh-h`, `.sh-x`, `.sh-s`, `.sh-list`, `.sh-foot` belong to the themed sheet in the WebUI.

## Tests

Run `sh tests/run_host_tests.sh` from anywhere. It runs the thread_local and range/limit lints, the host C++ tests (g++), a JavaScript syntax check and `tests/ui_test.py` (headless Chromium through Playwright; skipped when not installed).
These tests do not compile for Android and do not run on a phone. The `.so` is built by GitHub Actions; check the build and test on the device before calling a release good.
