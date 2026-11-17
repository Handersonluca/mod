#!/usr/bin/env bash
set -euo pipefail
: "${ANDROID_NDK_HOME:?Defina ANDROID_NDK_HOME para o NDK r27c}"
native_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang++" \
  -std=c++17 -O2 -Wall -Wextra -Werror -fPIE -pie -static-libstdc++ \
  -Wl,-z,max-page-size=16384 \
  "$native_dir/native-v311.cpp" \
  -o "$native_dir/../app/src/main/jniLibs/arm64-v8a/libmemory_v311.so"
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang++" \
  -std=c++17 -O2 -Wall -Wextra -Werror -shared -fPIC -static-libstdc++ \
  -Wl,-z,max-page-size=16384 \
  "$native_dir/bypass-jni.cpp" \
  -o "$native_dir/../app/src/main/jniLibs/arm64-v8a/libarroz_jni.so"
