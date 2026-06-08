#!/bin/sh
# Run this once after cloning, or after deleting the build directory.
rm -rf build && mkdir build
PICO_SDK_PATH=$HOME/pico/pico-sdk cmake \
    -DPICO_BOARD=pico2_w \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/pico/pico-sdk/cmake/preload/toolchains/pico_arm_cortex_m33_gcc.cmake \
    -S . -B build
