#!/bin/bash
# RAM and CPU of the running app on a device over adb:
#   android/tools/measure.sh [seconds]
# Memory from dumpsys meminfo; CPU from /proc/<pid>/stat over an idle window
# and a window spent switching nav tabs (taps on the left strip only, which
# never send anything to the printer). CPU is % of one core.
secs=${1:-30}
pkg=io.github.pellcorp.grumpyscreen
pid=$(adb shell pidof $pkg | tr -d '\r')
[ -z "$pid" ] && { echo "$pkg is not running"; exit 1; }
hz=100  # USER_HZ on Android
res=$(adb shell wm size | awk '/Physical/ {print $3}' | tr -d '\r')
w=${res%x*}; h=${res#*x}; [ "$w" -lt "$h" ] && { t=$w; w=$h; h=$t; }  # landscape

ticks() { adb shell cat /proc/$pid/stat | awk '{print $14 + $15}'; }
cpu() {  # cpu <label> <command run for the window>
  local t0 t1 s0 s1
  t0=$(ticks); s0=$(date +%s.%N)
  eval "$2"
  t1=$(ticks); s1=$(date +%s.%N)
  awk -v a="$t0" -v b="$t1" -v s="$s0" -v e="$s1" -v hz=$hz -v l="$1" \
    'BEGIN { printf "CPU %-10s %5.1f%% of one core (%.0fs)\n", l, (b - a) / hz / (e - s) * 100, e - s }'
}

echo "device: $(adb shell getprop ro.product.model | tr -d '\r'), Android $(adb shell getprop ro.build.version.release | tr -d '\r'), ${w}x${h}, $(adb shell nproc | tr -d '\r') cores"
adb shell dumpsys meminfo $pkg | awk '/TOTAL PSS:/ {printf "RAM total PSS  %6.1f MB\n", $3/1024}
                                      /^ +Native Heap:/ {printf "RAM native heap %5.1f MB\n", $3/1024}'
cpu idle "sleep $secs"
# nav strip: home at the top, tools at the bottom
bar=$((w * 6 / 100)); xs=$((bar / 2))
tabs="$((h * 12 / 100)) $((h * 62 / 100)) $((h * 88 / 100))"
cpu switching 'end=$(( $(date +%s) + secs )); while [ $(date +%s) -lt $end ]; do for y in $tabs; do adb shell input tap $xs $y; sleep 0.5; done; done; adb shell input tap $xs $((h * 12 / 100))'
adb shell dumpsys meminfo $pkg | awk '/TOTAL PSS:/ {printf "RAM total PSS after switching %5.1f MB\n", $3/1024}'
