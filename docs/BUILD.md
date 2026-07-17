# Building SandHook

## Prerequisites

- Android NDK r21 or newer.
- Android Studio with CMake support OR a standalone CMake/NDK environment.

## Option 1: Integration into Android Studio (Recommended)

1. Place the `cpp` folder (containing `CMakeLists.txt` and `src`) and the `java` folder into your Android project's `app/src/main/` directory.
2. Update your `app/build.gradle` to link the native code:

```gradle
android {
    defaultConfig {
        ndk {
            abiFilters 'arm64-v8a'
        }
        externalNativeBuild {
            cmake {
                cppFlags "-std=c++14 -fno-exceptions -fno-rtti"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path "src/main/cpp/CMakeLists.txt"
        }
    }
}