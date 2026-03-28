# Skia for Friction

Fork of skia for use with Friction.

Skia is a complete 2D graphic library for drawing Text, Geometries, and Images.

## Linux/macOS

Note that Friction includes skia and will build it for you.

### Requirements

* ninja
* python3
* cmake
* clang
* expat
* freetype
* fontconfig
* libjpeg-turbo
* libpng
* libwebp
* zlib

### Options

* `-DSKIA_USE_SYSTEM_LIBS=OFF`
* `-DSKIA_SYNC_EXTERNAL=ON` *(not needed if using source tarball)*

Will build all dependencies instead of using system libraries.

### Build and install

```
mkdir build && cd build
cmake -G Ninja \
-DCMAKE_INSTALL_PREFIX=/usr \
-DCMAKE_CXX_COMPILER=clang++ \
-DCMAKE_C_COMPILER=clang ..
cmake --build .
```

```
cmake --install .
```

This will install `libskia-friction.so` to defined install path. Add optional `--prefix=/some/path` to install to a different location.

**Note:** Install option only available on Linux.

## Windows

### Requirements

* CMake, Python and Ninja in PATH
* LLVM (Installed to Program Files, v15 recommended)
* Visual Studio (Build Tools) 2017

### Build

```
cmake -A x64 -DSKIA_USE_SYSTEM_LIBS=OFF -DSKIA_SYNC_EXTERNAL=ON ..
cmake --build .
```

* `SKIA_SYNC_EXTERNAL=ON` requires git in PATH, not needed if using source tarball
