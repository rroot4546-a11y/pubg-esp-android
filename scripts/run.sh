#!/usr/bin/env bash
set -euo pipefail

# Host-side launcher. Edit DEVICE_ID / PACKAGE / SERVER as needed.

DEVICE_ID="${DEVICE_ID:-}"
PACKAGE="${PACKAGE:-com.tencent.ig}"
SERVER="${SERVER:-./frida-server-android-arm64}"
SCRIPT="${SCRIPT:-./esp_pubg.js}"
REMOTE="/data/local/tmp/frida-server"

ADB=(adb)
if [[ -n "$DEVICE_ID" ]]; then ADB+=( -s "$DEVICE_ID" ); fi

if [[ ! -f "$SERVER" ]]; then
  echo "[!] frida-server binary not found: $SERVER" >&2
  exit 1
fi

"${ADB[@]}" push "$SERVER" "$REMOTE"
"${ADB[@]}" shell chmod 755 "$REMOTE"
"${ADB[@]}" shell "su -c '$REMOTE -D' >/dev/null 2>&1 &" || true
sleep 1

echo "[+] attaching to $PACKAGE with $SCRIPT"
frida -U -f "$PACKAGE" -l "$SCRIPT"
