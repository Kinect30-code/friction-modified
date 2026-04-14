# Build Directories

## Keep This One

Use this as the main working build directory:

- [build_runfix_clang](/media/kinect/Project/friction-1.0.1/build_runfix_clang)

Why:

- it is the build directory that successfully compiled the current source tree
- it already matches the project's clang + skia setup
- it is the least risky path for future incremental builds

## What `runfix` Means

`runfix` is just an old folder name from a previous repair/build attempt.

It is not a special Friction system.
It is not a plugin system.
It is not a runtime mode.

Right now you can simply understand it as:

- `build_runfix_clang` = the current usable build folder

## Other Build Folders

- [build](/media/kinect/Project/friction-1.0.1/build)
- [build_backup_compat](/media/kinect/Project/friction-1.0.1/build_backup_compat)
- [build_isolation](/media/kinect/Project/friction-1.0.1/build_isolation)
- [build_isolation_clang](/media/kinect/Project/friction-1.0.1/build_isolation_clang)
- [build_runfix](/media/kinect/Project/friction-1.0.1/build_runfix)

These exist because of previous experiments, compatibility work, or alternate configure attempts.

## Recommended Rule

From now on:

1. Build only with `build_runfix_clang`
2. Treat the other build folders as disposable
3. Do not create a new build folder unless there is a very specific reason

## Safe Cleanup Plan

I have not deleted the other build folders yet.

If you want, the next step can be:

1. keep `build_runfix_clang`
2. keep one backup build only if you want
3. delete the rest

