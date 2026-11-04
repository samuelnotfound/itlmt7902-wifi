#!/bin/bash
# Read-only driver/network/app capture. No password collection.
set -u
destination="${1:-$HOME/Desktop/MT7902-WiFi-logs-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$destination"
sw_vers > "$destination/system.txt"
uname -m >> "$destination/system.txt"
ioreg -r -c itlmt7902 -l -w0 | sed -E 's/("itl_psk[0-9]*" = ).*/\1"<redacted>"/' > "$destination/driver-ioreg.txt"
ioreg -r -c itlmt7902Eth -l -w0 > "$destination/ethernet-ioreg.txt"
kmutil showloaded > "$destination/loaded-kexts.txt" 2>&1
networksetup -listallhardwareports > "$destination/network-ports.txt" 2>&1
ifconfig -a > "$destination/interfaces.txt" 2>&1
# Driver logs are also mirrored into m0/m1/m2/m3-log and wifi-events in ioreg.
log show --last 10m --info --debug --style compact \
    --predicate 'eventMessage CONTAINS "itlmt7902" OR process == "HeliPort-MT7902" OR subsystem == "org.samuelnotfound.HeliPortMT7902"' \
    > "$destination/unified-log.txt" 2>&1
ps -axo pid,state,etime,comm | awk 'NR == 1 || /HeliPort-MT7902|ItlWiFi7902/' \
    > "$destination/app-processes.txt" 2>&1
# Copy at most three existing app reports; never request another crash.
reports="$HOME/Library/Logs/DiagnosticReports"
ls -t "$reports"/HeliPort-MT7902*.ips "$reports"/HeliPort-MT7902*.crash 2>/dev/null | head -n 3 | \
    while IFS= read -r report; do
        [ -f "$report" ] && [ ! -L "$report" ] || continue
        bytes=$(wc -c < "$report")
        [ "$bytes" -le 8388608 ] || continue
        mkdir -p "$destination/app-crash-reports"
        cp -p "$report" "$destination/app-crash-reports/"
    done
printf '日志已保存：%s\n' "$destination"
printf '日志含附近网络名称、MAC 和 IP；不包含 Wi-Fi 密码。\n'
