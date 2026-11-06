#!/bin/bash
set -e

BASE_DIR="/home/aju/Android/05_Device_Fixes_and_Apps/Cat_S22_Flip_ARM64_Patches"
CLANG="/home/aju/Android/Tools/prebuilts/clang/host/linux-x86/clang-r563880c/bin/clang"
STRIP="/home/aju/Android/Tools/prebuilts/clang/host/linux-x86/clang-r563880c/bin/llvm-strip"
SYSROOT="/home/aju/Android/Sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/linux-x86_64/sysroot"

CFLAGS=(
    --target=aarch64-linux-android30
    --sysroot="$SYSROOT"
    -fuse-ld=lld
    -O2
    -Wall
    -fPIE
    -pie
)

mkdir -p "$BASE_DIR/daemons"

echo "[+] Compiling 64-bit (ARM64 / aarch64) Cat S22 Flip Daemons..."

"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/outerscreen/cat_boot_display.c"    -o "$BASE_DIR/daemons/cat_boot_display"
"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/keypad/cat_hall_keylight.c"        -o "$BASE_DIR/daemons/cat_hall_keylight"
"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/hall_service/cat_hall_wake_evdev.c" -o "$BASE_DIR/daemons/cat_hall_wake"
"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/haptic_service/cat_haptic_service.c" -o "$BASE_DIR/daemons/cat_haptic_service"
"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/ptt_service/cat_ptt_service.c"     -o "$BASE_DIR/daemons/cat_ptt_service"
"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/outerscreen/cat_watchdog.c"        -o "$BASE_DIR/daemons/cat_watchdog"
"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/keypad/keypad_debounce_smart.c"    -o "$BASE_DIR/daemons/keypad_debounce_clean"
"$CLANG" "${CFLAGS[@]}" "$BASE_DIR/outerscreen/outerscreen_service.c" -lm -o "$BASE_DIR/daemons/outerscreen_service"

# Also update binaries in their respective module folders
cp "$BASE_DIR/daemons/cat_boot_display"      "$BASE_DIR/outerscreen/cat_boot_display"
cp "$BASE_DIR/daemons/cat_hall_keylight"     "$BASE_DIR/keypad/cat_hall_keylight"
cp "$BASE_DIR/daemons/cat_hall_wake"         "$BASE_DIR/hall_service/cat_hall_wake"
cp "$BASE_DIR/daemons/cat_haptic_service"    "$BASE_DIR/haptic_service/cat_haptic_service"
cp "$BASE_DIR/daemons/cat_ptt_service"       "$BASE_DIR/ptt_service/cat_ptt_service"
cp "$BASE_DIR/daemons/cat_watchdog"          "$BASE_DIR/outerscreen/cat_watchdog"
cp "$BASE_DIR/daemons/keypad_debounce_clean" "$BASE_DIR/keypad/keypad_debounce_clean"
cp "$BASE_DIR/daemons/outerscreen_service"   "$BASE_DIR/outerscreen/outerscreen_service"

# Copy shell scripts and init rc into daemons / init_and_scripts
cp "$BASE_DIR/system_init/cat_ims_daemon.sh" "$BASE_DIR/daemons/cat_ims_daemon.sh"
cp "$BASE_DIR/system_init/cat_perf.sh"       "$BASE_DIR/daemons/cat_perf.sh"
chmod +x "$BASE_DIR/daemons/"*

echo "[+] Verifying ELF 64-bit LSB aarch64 binaries:"
file "$BASE_DIR/daemons/"*
