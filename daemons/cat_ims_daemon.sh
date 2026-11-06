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

    sleep 120
done
