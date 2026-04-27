# Stability Release Checklist

This is the final pre-merge checklist for promoting the current branch to a
stable mainline.

It is intentionally narrow:

- build must stay green
- preview/cache/export regressions must be exercised on real projects
- hidden memory/runtime issues should be checked with the ASan build

## 1. Automated Scan

Run the automated gate first:

```bash
tools/run_stability_scan.sh
```

Default behavior:

- builds `build` target `friction`
- builds `build_asan_clang` target `vecb`
- runs focused `clang-tidy` with `clang-analyzer-*`
- runs the existing first-party `cppcheck` wrapper
- writes logs to `/tmp/friction_stability_scan`

Useful overrides:

```bash
JOBS=8 tools/run_stability_scan.sh
BUILD_DIR=build ASAN_BUILD_DIR=build_asan_clang tools/run_stability_scan.sh
LOG_DIR=/tmp/friction_stable_gate tools/run_stability_scan.sh
CLANG_TIDY_CHECKS='-*,clang-analyzer-*,bugprone-use-after-move' tools/run_stability_scan.sh
```

Review these outputs before merge:

- `/tmp/friction_stability_scan/build_release.log`
- `/tmp/friction_stability_scan/build_asan.log`
- `/tmp/friction_stability_scan/clang_tidy_focus.log`
- `/tmp/friction_stability_scan/clang_tidy_firstparty.log`
- `/tmp/friction_stability_scan/cppcheck_firstparty.txt`
- `/tmp/friction_stability_scan/summary.txt`

## 2. ASan Smoke Session

When chasing hidden crashes or allocator bugs, launch the ASan build directly:

```bash
ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-18 \
ASAN_OPTIONS=alloc_dealloc_mismatch=1:halt_on_error=1:abort_on_error=1:symbolize=1:fast_unwind_on_malloc=0 \
build_asan_clang/src/app/vecb
```

Use the ASan build for smoke if any of these are true:

- preview stalls are back
- export stalls are back
- opening older projects crashes
- clearing cache crashes
- a static-analysis report mentions lifetime or deallocation issues

## 3. Manual Smoke Checklist

Use at least one light project and one heavy real project.
The heavy project should include video, track matte, cache pressure, and export.

1. Open a previously saved project that used to expose preview/cache issues.
2. Confirm opening the composition does not auto-play the viewport before user input.
3. Press Space to play and pause repeatedly from:
   - frame 0
   - a clip start
   - a keyframe boundary
   - a loop boundary
4. Scrub the timeline aggressively across the in/out range and around frame 0.
5. Verify preview does not get stuck in a frame bounce pattern such as `7, 8, 7, 8`.
6. Change preview resolution and change it back. Playback should not require this as a recovery step.
7. Trigger cache clear with `Ctrl+R`. It must not throw, freeze, or spam memory-probe failures.
8. Test loop playback over a short region and confirm the jump back to the loop start does not stall.
9. Open an older project after working in a newer one. File reload should not freeze the app.

## 4. Export Checklist

Run exports on the same heavy project.

1. Export MP4 and verify the progress reaches completion.
2. Export WebM and verify the output file is playable.
3. If the composition contains transparency, export the VP9 alpha preset and verify the file is valid.
4. Export to a path where a same-name file already exists and confirm the overwrite prompt appears.
5. Re-open the exported file in an external player and confirm it is not truncated or corrupt.
6. Verify export no longer stalls at the first few frames or at 99 percent.

If export regresses, rerun with:

```bash
FRICTION_EXPORT_DEBUG=1 build_asan_clang/src/app/vecb
```

Then capture the export log before changing code.

## 5. Known Noise That Is Not a Release Blocker By Itself

These are worth cleaning, but they should not block a stable branch by
themselves if runtime behavior is correct:

- `complexanimator.h` missing `override` warning spam
- old macro style in `simplemath.h`, `exceptions.h`, and smart-pointer helpers
- `cppcheck` noise caused by deep third-party include chains

## 6. Merge Gate

Treat the branch as merge-ready only when all of the following are true:

1. `tools/run_stability_scan.sh` completes successfully.
2. No new ASan crash appears during the smoke session.
3. Preview, cache clear, and export all pass on a heavy real project.
4. Any remaining warnings are known debt, not a newly introduced risk.
