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
# Override init.cat.rc's broken dirty_ratio=99 / dirty_background_ratio=50
echo 15 > /proc/sys/vm/dirty_ratio 2>/dev/null
echo 5 > /proc/sys/vm/dirty_background_ratio 2>/dev/null
echo 500 > /proc/sys/vm/dirty_expire_centisecs 2>/dev/null
echo 200 > /proc/sys/vm/dirty_writeback_centisecs 2>/dev/null
echo 0 > /proc/sys/vm/page-cluster 2>/dev/null

# 4. I/O Scheduler & Read-Ahead Optimization for eMMC
for queue in /sys/block/mmcblk*/queue; do
    if [ -d "$queue" ]; then
        echo 512 > "$queue/read_ahead_kb" 2>/dev/null
        echo 0 > "$queue/iostats" 2>/dev/null
        echo 0 > "$queue/add_random" 2>/dev/null
    fi
done

# 5. Persistent VoLTE & IMS Network Activation + Single SIM Mode (Survives Factory Reset)
resetprop_phh persist.radio.multisim.config ssss
setprop persist.radio.multisim.config ssss
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

# 7. Custom Branding & Attribution
resetprop_phh ro.voltage.build.status "GSI modded for CAT S22 FLIP"
resetprop_phh ro.voltage.buildtype "GSI modded for CAT S22 FLIP"
resetprop_phh ro.voltage.maintainer "ajugeorge"
resetprop_phh ro.voltage.version "VoltageOS-4.4-CAT-S22-ajugeorge"
setprop ro.voltage.build.status "GSI modded for CAT S22 FLIP"
setprop ro.voltage.buildtype "GSI modded for CAT S22 FLIP"
setprop ro.voltage.maintainer "ajugeorge"
setprop ro.voltage.version "VoltageOS-4.4-CAT-S22-ajugeorge"

# 8. Prevent Android from sleeping outer screen (SPI bus power gating fix)
settings put system screen_off_timeout 1800000
settings put global low_power 0
echo "outerscreen_wakelock" > /sys/power/wake_lock 2>/dev/null

# 9. Low-RAM (2GB) & Kernel 4.19 LMKD + Axion Performance Tuning
setprop persist.sys.phh.lmk_tweaks true
setprop persist.sys.phh.axion_props true
device_config put activity_manager max_cached_processes 8 2>/dev/null
device_config put activity_manager max_phantom_processes 8 2>/dev/null

# 10. UI Smoothness: 0.0x Instant Animations, Disable Blur & Enable Scroll Boost
settings put global window_animation_scale 0.0
settings put global transition_animation_scale 0.0
settings put global animator_duration_scale 0.0
settings put global disable_window_blurs 1
setprop persist.sys.phh.scroll_boost true
setprop persist.sys.phh.sf_disable_backpressure 1
setprop persist.sys.phh.sf_auto_latch_unsignaled true
setprop debug.sf.latch_unsignaled 1
setprop debug.sf.disable_backpressure 1

# 11. Trim Unneeded Google & Axion Background Services (Edge Bar, ThemePicker, Traceur, GMS Ble)
stop traced 2>/dev/null
stop traced_probes 2>/dev/null
pm disable-user --user 0 com.google.android.partnersetup 2>/dev/null
pm disable-user --user 0 com.android.edge.bar 2>/dev/null
pm disable-user --user 0 com.android.axion.themepicker 2>/dev/null
pm disable-user --user 0 com.android.traceur 2>/dev/null
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

# 12. Stock Launcher (com.android.launcher3) & TT9 Optimization & RAM Pinning
pm enable com.android.launcher3 2>/dev/null
cmd package set-home-activity com.android.launcher3/.uioverrides.QuickstepLauncher 2>/dev/null
cmd deviceidle whitelist +com.android.launcher3 2>/dev/null
cmd deviceidle whitelist +io.github.sspanak.tt9 2>/dev/null
cmd appops set com.android.launcher3 RUN_IN_BACKGROUND allow 2>/dev/null
cmd appops set com.android.launcher3 RUN_ANY_IN_BACKGROUND allow 2>/dev/null
cmd appops set io.github.sspanak.tt9 RUN_IN_BACKGROUND allow 2>/dev/null
cmd appops set io.github.sspanak.tt9 RUN_ANY_IN_BACKGROUND allow 2>/dev/null

# Pin Stock Launcher, SystemUI, and TT9 in RAM so LMKD never evicts them
for pkg in com.android.launcher3 com.android.systemui io.github.sspanak.tt9; do
    pid=$(pidof "$pkg")
    if [ -n "$pid" ]; then
        echo -800 > "/proc/$pid/oom_score_adj" 2>/dev/null
    fi
done

# AOT speed compile Stock Launcher, SystemUI, and TT9 once if not already compiled
if [ ! -f /data/local/tmp/.cat_aot_done ]; then
    (
        cmd package compile -m speed -f com.android.launcher3 2>/dev/null
        cmd package compile -m speed -f com.android.systemui 2>/dev/null
        cmd package compile -m speed -f io.github.sspanak.tt9 2>/dev/null
        touch /data/local/tmp/.cat_aot_done
    ) &
fi

# 13. Heavy / Banking Apps RAM Protection & Process Keep-Alive
# Protect PhonePe & Federal Bank (FedMobile) from aggressive background trimming
cmd deviceidle whitelist +com.phonepe.app 2>/dev/null
cmd deviceidle whitelist +com.fedmobile 2>/dev/null
cmd appops set com.phonepe.app RUN_IN_BACKGROUND allow 2>/dev/null
cmd appops set com.fedmobile RUN_IN_BACKGROUND allow 2>/dev/null
cmd appops set com.phonepe.app RUN_ANY_IN_BACKGROUND allow 2>/dev/null
cmd appops set com.fedmobile RUN_ANY_IN_BACKGROUND allow 2>/dev/null

# 14. Expand Low-Memory Headroom on 2GB RAM
# Aggressively page inactive anonymous memory into zRAM and drop stale caches
echo 100 > /proc/sys/vm/swappiness 2>/dev/null
echo 100 > /proc/sys/vm/vfs_cache_pressure 2>/dev/null
echo 1 > /proc/sys/vm/compact_memory 2>/dev/null


