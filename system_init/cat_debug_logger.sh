#!/system/bin/sh

# Ensure target log directory exists on internal storage
LOG_DIR="/sdcard/CatLogs"
mkdir -p "$LOG_DIR"
chmod 777 "$LOG_DIR"

LOG_FILE="$LOG_DIR/cat_system.log"

echo "=== Cat S22 Flip Persistent Debug Logger Started: $(date) ===" >> "$LOG_FILE"

# Background continuous logcat capture focused on hardware/displays/sensors/daemons
logcat -b all -v time -f "$LOG_FILE" -r 4096 -n 4 \
  "*:S" \
  "cat_hall_keylight:V" \
  "outerscreen_service:V" \
  "keypad_debounce_clean:V" \
  "cat_ptt_service:V" \
  "PowerManagerService:I" \
  "DisplayPowerController:I" \
  "InputReader:I" \
  "mdss_fb:V" \
  "mdss_spi:V" \
  "hall_sensor:V" \
  "avc:V" &

# Secondary periodic snapshot of hardware sysfs status every 5 seconds
while true; do
    echo "--- $(date) ---" >> "$LOG_DIR/hardware_state.txt"
    echo "outerscreen_service: $(getprop init.svc.outerscreen)" >> "$LOG_DIR/hardware_state.txt"
    echo "cat_keylight: $(getprop init.svc.cat_keylight)" >> "$LOG_DIR/hardware_state.txt"
    echo "battery status: $(cat /sys/class/power_supply/battery/status 2>/dev/null)" >> "$LOG_DIR/hardware_state.txt"
    echo "battery level: $(cat /sys/class/power_supply/battery/capacity 2>/dev/null)" >> "$LOG_DIR/hardware_state.txt"
    echo "lcd-backlight2: $(cat /sys/class/leds/lcd-backlight2/brightness 2>/dev/null)" >> "$LOG_DIR/hardware_state.txt"
    echo "keyboard_light: $(cat /sys/class/leds/keyboard_light/brightness 2>/dev/null)" >> "$LOG_DIR/hardware_state.txt"
    echo "fb2 blank: $(cat /sys/class/graphics/fb2/blank 2>/dev/null)" >> "$LOG_DIR/hardware_state.txt"
    echo "" >> "$LOG_DIR/hardware_state.txt"
    sleep 5
done
