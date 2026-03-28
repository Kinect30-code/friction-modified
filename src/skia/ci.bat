@echo off

set CWD=%cd%

git submodule update --init

if exist "build\" (
    @RD /S /Q build
)
mkdir build
cd "%CWD%\build"

cmake -A x64 -DSKIA_USE_SYSTEM_LIBS=OFF -DSKIA_SYNC_EXTERNAL=ON ..
cmake --build .

