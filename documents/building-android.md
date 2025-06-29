# Building for Android

This document outlines the basic steps required to build **shadPS4** for Android
using the Android NDK and CMake. The instructions are aimed at API level 16
and ARM64 (aarch64) devices.

## Prerequisites

- Android Studio with the NDK component installed
- CMake 3.24 or newer

## Steps

1. Open the `android` directory in Android Studio.
2. When prompted, let Android Studio download any missing SDK packages.
3. Build the `app` module which will compile the core emulator as a shared
   library and package it into an APK.

The Android project uses the root CMake build to generate `libshadps4.so` which
is then loaded from Java/Kotlin code.

The sample UI included is minimal and serves as a starting point. It exposes a
text box to input the game path and a button to launch the emulator. Further
work is required to create a fully featured interface comparable to the Dolphin
emulator.
