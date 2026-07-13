# Building SandHook

## Prerequisites

- Android NDK r23+ or aarch64-linux-android-g++.
- Termux with `g++`, `cmake`, `make`.

## Build with CMake

```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a ..
make