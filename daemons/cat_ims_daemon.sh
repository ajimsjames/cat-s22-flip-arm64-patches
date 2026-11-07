#!/system/bin/sh
# Cat S22 Flip — Persistent IMS Cellular Network Daemon
# Holds the cellular IMS packet bearer active continuously (NET_CAPABILITY_IMS)

while true; do
    resetprop_phh persist.dbg.volte_avail_ovr 1
    resetprop_phh persist.dbg.volte_avail_ovr0 1
    resetprop_phh persist.dbg.volte_avail_ovr1 1
    resetprop_phh persist.dbg.volte_avail_ovr2 1
    resetprop_phh persist.dbg.vt_avail_ovr 1
    resetprop_phh persist.dbg.wfc_avail_ovr 1
    resetprop_phh persist.dbg.allow_ims_off 0
    resetprop_phh persist.sys.phh.allow_binder_thread_on_incoming_calls 1
    resetprop_phh ro.telephony.block_binder_thread_on_incoming_calls false
    resetprop_phh persist.sys.phh.ims.caf true
    resetprop_phh persist.sys.phh.ims.floss false
    resetprop_phh ro.telephony.default_network 9,9

    # Ensure QM215 LTE/WCDMA/GSM network bitmask (326655) is active and never stuck on 4096 (LTE-only without 2G/3G or NR-invalid)
    if [ "$(getprop sys.boot_completed)" = "1" ]; then
        cur_net=$(settings get global preferred_network_mode)
        if [ "$cur_net" = "33" ] || [ "$cur_net" = "26" ] || [ "$cur_net" = "null" ]; then
            settings put global preferred_network_mode 9
            settings put global preferred_network_mode1 9
        fi
        cmd phone set-allowed-network-types-for-users -s 0 01001111101111111111 >/dev/null 2>&1
    fi

    sleep 120
done
