#!/system/bin/sh

# 1. End Call Button Configuration:
# Enable Power Button Ends Call (incall_power_button_behavior = 2)
# The Red End Call button (☎ / ⏻) is wired to KEY_POWER on qpnp_pon.
settings put secure incall_power_button_behavior 2

# 2. CPU Schedutil Governor Tweaks (instantaneous clock ramp-up)
for gov in /sys/devices/system/cpu/cpufreq/policy*/schedutil /sys/devices/system/cpu/cpu*/cpufreq/schedutil; do
    if [ -d "$gov" ]; then
        echo 0 > "$gov/up_rate_limit_us" 2>/dev/null
        echo 10000 > "$gov/down_rate_limit_us" 2>/dev/null
        echo 1 > "$gov/iowait_boost_enable" 2>/dev/null
    fi
done

# 3. Linux Kernel Virtual Memory & Cache Tuning
echo 60 > /proc/sys/vm/swappiness 2>/dev/null
echo 50 > /proc/sys/vm/vfs_cache_pressure 2>/dev/null
echo 15 > /proc/sys/vm/dirty_ratio 2>/dev/null
echo 5 > /proc/sys/vm/dirty_background_ratio 2>/dev/null
echo 500 > /proc/sys/vm/dirty_expire_centisecs 2>/dev/null
echo 200 > /proc/sys/vm/dirty_writeback_centisecs 2>/dev/null

# 4. I/O Scheduler & Read-Ahead Optimization for eMMC
for queue in /sys/block/mmcblk*/queue; do
    if [ -d "$queue" ]; then
        echo 512 > "$queue/read_ahead_kb" 2>/dev/null
        echo 0 > "$queue/iostats" 2>/dev/null
        echo 0 > "$queue/add_random" 2>/dev/null
    fi
done

# 5. Persistent VoLTE & IMS Network Activation (Survives Factory Reset)
resetprop_phh persist.dbg.volte_avail_ovr 1
resetprop_phh persist.dbg.volte_avail_ovr0 1
resetprop_phh persist.dbg.volte_avail_ovr1 1
resetprop_phh persist.dbg.volte_avail_ovr2 1
resetprop_phh persist.dbg.vt_avail_ovr 1
resetprop_phh persist.dbg.wfc_avail_ovr 1
resetprop_phh persist.dbg.allow_ims_off 1
resetprop_phh persist.sys.phh.ims.caf true
resetprop_phh persist.sys.phh.ims.floss false

# 6. Display Density & Compact UI Metrics
wm density 210 2>/dev/null

# 7. VoltageOS Custom Branding & Attribution
# Settings.apk VoltageMaintainerPreferenceController:
# summary = getBuildStatus() + " by " + getString(R.string.voltage_maintainer)
# When ro.voltage.build.status is set, getBuildStatus() returns that exact string.
# Overlay overrides R.string.voltage_maintainer to "ajugeorge".
# Combined summary display: "GSI modded for CAT S22 FLIP by ajugeorge"
resetprop_phh ro.voltage.build.status "GSI modded for CAT S22 FLIP"
resetprop_phh ro.voltage.buildtype "GSI modded for CAT S22 FLIP"
resetprop_phh ro.voltage.maintainer "ajugeorge"
resetprop_phh ro.voltage.version "VoltageOS-4.4-CAT-S22-ajugeorge"
setprop ro.voltage.build.status "GSI modded for CAT S22 FLIP"
setprop ro.voltage.buildtype "GSI modded for CAT S22 FLIP"
setprop ro.voltage.maintainer "ajugeorge"
setprop ro.voltage.version "VoltageOS-4.4-CAT-S22-ajugeorge"

# 8. Prevent Android from sleeping outer screen (SPI bus power gating fix)
# NotificationShade forces userActivityTimeout=10000ms via window attrs.
# When Android enters DOZE, the SPI bus is power-gated and outerscreen_service
# loses access to /dev/graphics/fb2. The outerscreen_service now holds its own
# kernel wake lock, but we also prevent premature doze from the settings side.
settings put system screen_off_timeout 1800000
settings put global low_power 0
# Acquire a kernel-level wake lock to prevent SPI bus power gating during DOZE.
# outerscreen_service writes this at startup, but ensure it survives reboots.
echo "outerscreen_wakelock" > /sys/power/wake_lock 2>/dev/null

# 9. Infinity-X Low-RAM (2GB) & Kernel 4.9 LMKD + Axion Performance Tuning
setprop persist.sys.phh.lmk_tweaks true
setprop persist.sys.phh.axion_props true

# 10. UI Smoothness: 0.5x Snappy Animations, Disable Blur & Enable Scroll Boost
settings put global window_animation_scale 0.5
settings put global transition_animation_scale 0.5
settings put global animator_duration_scale 0.5
settings put global disable_window_blurs 1
setprop persist.sys.phh.scroll_boost true
setprop persist.sys.phh.sf_disable_backpressure 1
setprop persist.sys.phh.sf_auto_latch_unsignaled true

# 11. Trim Unneeded Google Background Services (Nearby Share, Wearable, Offline BLE Beacon, PartnerSetup, Telemetry)
pm disable-user --user 0 com.google.android.partnersetup 2>/dev/null
for svc in \
    "com.google.android.gms/.nearby.discovery.service.DiscoveryService" \
    "com.google.android.gms/.nearby.presence.service.LifeCycleService" \
    "com.google.android.gms/.nearby.sharing.ReceiveSurfaceService" \
    "com.google.android.gms/.nearby.sharing.SendSurfaceService" \
    "com.google.android.gms/.wearable.service.WearableService" \
    "com.google.android.gms/.findmydevice.spot.bleadvertising.OfflineBeaconService_Persistent" \
    "com.google.android.gms/.clearcut.debug.ClearcutDebugDumpService" \
    "com.google.android.gms/.tron.CollectionService"; do
    pm disable "$svc" 2>/dev/null
done

# 12. Heavy / Banking Apps RAM Protection & Process Keep-Alive
# Protect PhonePe & Federal Bank (FedMobile) from aggressive background trimming
cmd deviceidle whitelist +com.phonepe.app 2>/dev/null
cmd deviceidle whitelist +com.fedmobile 2>/dev/null
cmd appops set com.phonepe.app RUN_IN_BACKGROUND allow 2>/dev/null
cmd appops set com.fedmobile RUN_IN_BACKGROUND allow 2>/dev/null
cmd appops set com.phonepe.app RUN_ANY_IN_BACKGROUND allow 2>/dev/null
cmd appops set com.fedmobile RUN_ANY_IN_BACKGROUND allow 2>/dev/null

# 13. Expand Low-Memory Headroom on 2GB RAM
# Aggressively page inactive anonymous memory into zRAM to leave physical RAM free for active banking apps
echo 100 > /proc/sys/vm/swappiness 2>/dev/null
echo 100 > /proc/sys/vm/vfs_cache_pressure 2>/dev/null

