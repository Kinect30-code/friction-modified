# System Audit 2026-04-13

## Backup

- Working tree backup created at `/tmp/friction-1.0.1-backup-20260413-004954.tar.gz`
- Backup excludes disposable build outputs and preserves the current modified/untracked source tree

## Tooling Baseline

- `cppcheck 2.13.0`: available
- `clang-tidy`: not installed
- `scan-build`: not installed
- `include-what-you-use`: not installed
- Active build directory: `build_runfix_clang`

## Static Analysis Findings Worth Acting On

### Fixed

1. `src/core/hardwareinfo.cpp`
- `getTotalRamBytes()` had a missing non-void return path outside the platform branches.
- Added an explicit unsupported-platform throw plus fallback return.

2. `src/core/Segments/fitcurves.cpp`
- Temporary Bezier buffers used during tangent search were allocated uninitialized.
- Switched the buffers to zero-initialized allocations to remove the `cppcheck` uninitialized-data risk.

3. `src/core/CacheHandlers/hddcachablecachehandler.h`
- `end()` incorrectly returned `begin()`.
- This broke range-based iteration over the cache handler and made `Canvas::sceneFramesUpToDate()` effectively iterate over nothing.

4. `src/app/renderhandler.cpp`
- Removed duplicate preview-window queueing on cache miss.
- Added cached preview-window application so identical range requests do not reschedule audio/cache work and repaint all visible scenes again.
- Batched non-loop preview cache trimming instead of shrinking the use range every frame.

## Runtime / Build Findings

- The project configures and builds from `build_runfix_clang`.
- Current build output is dominated by pre-existing warning classes:
  - missing `override`
  - deprecated FFmpeg channel-layout APIs
- No new warning class specific to the fixes above was introduced during validation.

## Safe Cleanup Candidates

These look redundant but were not deleted in this pass because they may still be serving as manual fallback build trees:

- `build`
- `build_backup_compat`
- `build_isolation`
- `build_isolation_clang`
- `build_runfix`

Recommended rule:

- keep `build_runfix_clang` as the only active build directory
- remove the others only after one more successful local run confirms they are no longer needed

## AE Alignment Status

The project is closer to AE-style workflow than stock Friction, but it is not fully aligned yet.

What is already present:

- AE-style shortcut definitions and controller scaffold
- mask workflow module and reveal-oriented timeline work
- precompose/mask/time-remap/freeze-frame entry points

What is still visibly incomplete:

1. `src/app/GUI/aeshortcutcontroller.cpp`
- `handleRevealShortcut()` currently returns `false`, so the AE-style reveal flow is not centralized in the controller yet.

2. `docs/documentation/ai-handoff-2026-03-27.md`
- documents that adjustment layers are still a working first pass, not full AE semantics
- documents that several mask/effect interactions are implemented but still need hardening

## Performance Follow-Ups

### Already Improved In This Pass

- less preview-cache churn while playing forward without loop
- less duplicate preview scheduling on cache misses
- less unnecessary scene-update fanout when the queued preview window has not changed

### Highest-Value Next Steps

1. Clean up warning debt in the animation/property hierarchy
- the project has a very large amount of missing-`override` noise
- this hides real compiler warnings and slows down review

2. Migrate deprecated FFmpeg channel-layout calls
- the old API is still used in several audio/cache paths
- this is both maintenance debt and future compatibility risk

3. Profile real preview sessions
- use an actual heavy scene and measure:
  - cache miss frequency
  - time spent in `Document::updateScenes()`
  - time spent in sound-cache scheduling
  - time spent in preview render queue refill

4. Finish AE reveal routing consolidation
- move reveal ownership to one path
- reduce duplicated logic between timeline widgets, dock widget routing, and shortcut handling
