#!/bin/bash
set -e

BASE_DIR="/home/aju/Android/05_Device_Fixes_and_Apps/Cat_S22_Flip_ARM64_Patches"

echo "[+] Waiting for ADB device..."
adb wait-for-device

echo "[+] Pushing ARM64 staging files to /data/local/tmp/cat_arm64..."
adb shell "rm -rf /data/local/tmp/cat_arm64 && mkdir -p /data/local/tmp/cat_arm64"
adb push "$BASE_DIR/daemons"              /data/local/tmp/cat_arm64/
adb push "$BASE_DIR/init_and_scripts"     /data/local/tmp/cat_arm64/
adb push "$BASE_DIR/keypad"               /data/local/tmp/cat_arm64/
adb push "$BASE_DIR/apps_and_permissions" /data/local/tmp/cat_arm64/
adb push "$BASE_DIR/overlays"             /data/local/tmp/cat_arm64/
adb push "$BASE_DIR/audio_configs"        /data/local/tmp/cat_arm64/

echo "[+] Installing into /system and /vendor..."
adb shell "su -c '
set -e
mount -o rw,remount /
mount -o rw,remount /vendor

# 1. Daemons & Scripts (/system/bin/)
for f in cat_boot_display cat_hall_keylight cat_hall_wake cat_haptic_service cat_ims_daemon.sh cat_perf.sh cat_ptt_service cat_watchdog keypad_debounce_clean outerscreen_service; do
    cp /data/local/tmp/cat_arm64/daemons/\$f /system/bin/\$f
    chown root:shell /system/bin/\$f
    chmod 0755 /system/bin/\$f
    chcon u:object_r:system_file:s0 /system/bin/\$f
done

# 2. Init RC (/system/etc/init/)
cp /data/local/tmp/cat_arm64/init_and_scripts/cat_hardware.rc /system/etc/init/cat_hardware.rc
chown root:root /system/etc/init/cat_hardware.rc
chmod 0644 /system/etc/init/cat_hardware.rc
chcon u:object_r:system_file:s0 /system/etc/init/cat_hardware.rc

# 3. Keypad Configs (/system/usr/idc/ & /system/usr/keylayout/)
cp \"/data/local/tmp/cat_arm64/keypad/soc:matrix_keypad.idc\" \"/system/usr/idc/soc:matrix_keypad.idc\"
cp \"/data/local/tmp/cat_arm64/keypad/soc:matrix_keypad.kl\"  \"/system/usr/keylayout/soc:matrix_keypad.kl\"
chmod 0644 \"/system/usr/idc/soc:matrix_keypad.idc\" \"/system/usr/keylayout/soc:matrix_keypad.kl\"
chcon u:object_r:system_file:s0 \"/system/usr/idc/soc:matrix_keypad.idc\" \"/system/usr/keylayout/soc:matrix_keypad.kl\"

# 4. Apps & Permissions (/system/priv-app/ & /system/etc/permissions/)
mkdir -p /system/priv-app/CatParts /system/priv-app/ims
cp /data/local/tmp/cat_arm64/apps_and_permissions/CatParts.apk /system/priv-app/CatParts/CatParts.apk
cp /data/local/tmp/cat_arm64/apps_and_permissions/ims.apk      /system/priv-app/ims/ims.apk
cp /data/local/tmp/cat_arm64/apps_and_permissions/privapp-permissions-catparts.xml /system/etc/permissions/privapp-permissions-catparts.xml
chmod 0755 /system/priv-app/CatParts /system/priv-app/ims
chmod 0644 /system/priv-app/CatParts/CatParts.apk /system/priv-app/ims/ims.apk /system/etc/permissions/privapp-permissions-catparts.xml
chcon -R u:object_r:system_file:s0 /system/priv-app/CatParts /system/priv-app/ims /system/etc/permissions/privapp-permissions-catparts.xml

# 5. Overlays (/system/product/overlay/)
for apk in /data/local/tmp/cat_arm64/overlays/*.apk; do
    name=\$(basename \"\$apk\")
    cp \"\$apk\" \"/system/product/overlay/\$name\"
    chmod 0644 \"/system/product/overlay/\$name\"
    chcon u:object_r:system_file:s0 \"/system/product/overlay/\$name\"
done

# 6. Vendor Audio Configs (/vendor/etc/)
for xml in audio_platform_info.xml audio_platform_info_intcodec.xml audio_platform_info_mtp.xml audio_policy_configuration.xml mixer_paths.xml mixer_paths_mtp.xml; do
    cp /data/local/tmp/cat_arm64/audio_configs/\$xml /vendor/etc/\$xml
    chmod 0644 /vendor/etc/\$xml
    chcon u:object_r:vendor_configs_file:s0 /vendor/etc/\$xml
done

sync
'"

echo "[+] All Cat S22 Flip ARM64 patches installed successfully!"
