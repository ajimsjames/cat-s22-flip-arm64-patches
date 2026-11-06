# Cat S22 Flip — ARM64 (64-bit) GSI Patches, Daemons, Overlays & Audio Configs

Complete 64-bit (`arm64-v8a` / `aarch64`) hardware support package for the **Cat S22 Flip (Qualcomm QM215)** running 64-bit Kernel 4.19 and 64-bit Treble GSIs (Android 15 / 16 / 17).

---

## 1. Cat S22 Specific 64-Bit Daemons & Services (`daemons/` & `init_and_scripts/`)

All native daemons are compiled as 64-bit `ELF 64-bit LSB pie executable, ARM aarch64` (`/system/bin/linker64`) via [`build_arm64_daemons.sh`](build_arm64_daemons.sh):

| Binary / Script | Target Path | Description |
| :--- | :--- | :--- |
| `cat_boot_display` | `/system/bin/cat_boot_display` | Streams live `/dev/kmsg` boot progress to the outer 128x128 OLED (`/dev/graphics/fb2`) until `sys.boot_completed=1`. |
| `cat_hall_keylight` | `/system/bin/cat_hall_keylight` | Controls physical keypad backlight based on flip state (`SW_LID`), key activity, and CatParts brightness/timeout settings. |
| `cat_hall_wake` | `/system/bin/cat_hall_wake` | Native `poll()` listener on `/dev/input/event*` (`hall_switch` `EV_SW`/`SW_LID`) dispatching `KEYCODE_WAKEUP` (224) / `KEYCODE_SLEEP` (223). |
| `cat_haptic_service` | `/system/bin/cat_haptic_service` | Low-latency 20ms tactile haptic feedback on physical keypad keypresses. |
| `cat_ims_daemon.sh` | `/system/bin/cat_ims_daemon.sh` | Persistent VoLTE / IMS property enforcement daemon. |
| `cat_perf.sh` | `/system/bin/cat_perf.sh` | Memory, ZRAM, and scheduler tuning for QM215 2GB RAM. |
| `cat_ptt_service` | `/system/bin/cat_ptt_service` | Programmable PTT button service (short press, long press, in-call answer/hangup/mute) + CatParts IPC bridge. |
| `cat_watchdog` | `/system/bin/cat_watchdog` | 15-second heartbeat watchdog that automatically recovers `/dev/graphics/fb2` and `outerscreen_service` if stalled. |
| `keypad_debounce_clean` | `/system/bin/keypad_debounce_clean` | 120ms hardware matrix keypad anti-chatter filter + Virtual Mouse mode (hold `*` for 2s) + Speaker key TAB/MENU mapper. |
| `outerscreen_service` | `/system/bin/outerscreen_service` | Multi-theme 128x128 outer OLED HUD (`fb2`) with clock, battery, notifications, media controls, and caller ID. |
| `cat_hardware.rc` | `/system/etc/init/cat_hardware.rc` | Android init configuration starting and managing all Cat S22 Flip hardware daemons. |

---

## 2. Cat S22 Keypad & Hardware Configurations (`keypad/`)

| File | Target Path | Description |
| :--- | :--- | :--- |
| `soc:matrix_keypad.idc` | `/system/usr/idc/soc:matrix_keypad.idc` | Input device configuration for the physical matrix keypad. |
| `soc:matrix_keypad.kl` | `/system/usr/keylayout/soc:matrix_keypad.kl` | Hardware keylayout mapping for the Cat S22 Flip keypad. |

---

## 3. Cat S22 Apps, Permissions & Overlays (`apps_and_permissions/` & `overlays/`)

| File | Target Path | Description |
| :--- | :--- | :--- |
| `CatParts.apk` | `/system/priv-app/CatParts/CatParts.apk` | Cat S22 Flip settings app (Outer Display HUD, Keypad Backlight, Programmable PTT, Virtual Mouse Speed). |
| `privapp-permissions-catparts.xml` | `/system/etc/permissions/privapp-permissions-catparts.xml` | Privileged permission whitelist for `com.cat.parts`. |
| `ims.apk` | `/system/priv-app/ims/ims.apk` | Qualcomm IMS service APK. |
| `CatCarrierConfigOverlay.apk` | `/system/product/overlay/CatCarrierConfigOverlay.apk` | CarrierConfig VoLTE/IMS overlay. |
| `CatTeleServiceOverlay.apk` | `/system/product/overlay/CatTeleServiceOverlay.apk` | TeleService VoLTE/phone overlay. |
| `treble-overlay-cat-s22-flip.apk` | `/system/product/overlay/treble-overlay-cat-s22-flip.apk` | Framework hardware overlay (lid behavior, in-call power hangup, 0dp navbar, D-pad focus highlight, volume steps). |
| `treble-overlay-cat-s22-flip-settings.apk` | `/system/product/overlay/treble-overlay-cat-s22-flip-settings.apk` | Settings maintainer summary overlay. |
| `treble-overlay-cat-s22-flip-systemui.apk` | `/system/product/overlay/treble-overlay-cat-s22-flip-systemui.apk` | SystemUI 3x3 compact Quick Settings layout overlay for 480x640 display. |
| `treble-overlay-cat-s22flip.apk` | `/system/product/overlay/treble-overlay-cat-s22flip.apk` | TrebleDroid property-matched (`Cat/S22FLIP/*`) framework overlay. |
| `treble-overlay-cat-s22flip-systemui.apk` | `/system/product/overlay/treble-overlay-cat-s22flip-systemui.apk` | TrebleDroid property-matched (`Cat/S22FLIP/*`) SystemUI overlay. |

---

## 4. Vendor Audio Routing Configs (`audio_configs/`)

Fixes both **in-call earpiece (`PRI_MI2S_RX`)** and **loudspeaker (`QUIN_MI2S_RX` + Awinic `AW881xx` SmartPA)** audio on 64-bit vendor (`msm8952-snd-card-mtp`):
- `audio_policy_configuration.xml` -> `/vendor/etc/audio_policy_configuration.xml`
- `audio_platform_info.xml` / `audio_platform_info_intcodec.xml` / `audio_platform_info_mtp.xml` -> `/vendor/etc/`
- `mixer_paths_mtp.xml` / `mixer_paths.xml` -> `/vendor/etc/`
