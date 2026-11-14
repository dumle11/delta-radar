#!/usr/bin/env bash
# Reset game account + full device fingerprint spoof.
# Requires: rooted device with Magisk (resetprop).
# Usage: ./reset_account.sh

set -euo pipefail

PKG="com.proxima.dfm"
SSAID_XML="/data/system/users/0/settings_ssaid.xml"

echo "[*] Stopping game..."
adb shell "am force-stop $PKG" 2>/dev/null || true
sleep 1

echo "[*] Clearing app data..."
adb shell "pm clear $PKG"

# ── Hardware ID spoofing via Magisk resetprop ──

RAND_SERIAL=$(python3 -c "import secrets; print(secrets.token_hex(8))")
RAND_GSM_SERIAL=$(python3 -c "import secrets; s=secrets.token_hex(12); print(s[:24])")
echo "[*] Spoofing serial number → $RAND_SERIAL"
adb shell "su -c 'resetprop ro.serialno $RAND_SERIAL'"
adb shell "su -c 'resetprop ro.boot.serialno $RAND_SERIAL'"
adb shell "su -c 'resetprop persist.radio.serialno $RAND_SERIAL'"
adb shell "su -c 'resetprop gsm.serial $RAND_GSM_SERIAL'"

# Spoof build fingerprint (keep same format, randomize build ID)
RAND_BUILD=$(python3 -c "import secrets; print(secrets.token_hex(4).upper())")
echo "[*] Spoofing build fingerprint → .../$RAND_BUILD/..."
adb shell "su -c 'resetprop ro.build.fingerprint \"OnePlus/OnePlus5/OnePlus5:10/QKQ1.191014.012/${RAND_BUILD}:user/release-keys\"'"
adb shell "su -c 'resetprop ro.build.display.id \"QKQ1.191014.012.${RAND_BUILD}\"'"

# ── SSAID (per-app Android ID, survives pm clear) ──

NEW_SSAID=$(python3 -c "import secrets; print(secrets.token_hex(8))")
echo "[*] Randomizing per-app SSAID → $NEW_SSAID"
OLD_SSAID=$(adb shell "su -c \"grep proxima $SSAID_XML\"" 2>/dev/null | sed -n 's/.*value=\"\([^\"]*\)\".*/\1/p' | head -1)
echo "    Old: ${OLD_SSAID:-unknown}"
adb shell "su -c 'sed -i \"s|package=\\\"$PKG\\\" defaultValue=\\\"[a-f0-9]*\\\"|package=\\\"$PKG\\\" defaultValue=\\\"$NEW_SSAID\\\"|g\" $SSAID_XML'"
adb shell "su -c 'sed -i \"s|name=\\\"10142\\\" value=\\\"[a-f0-9]*\\\"|name=\\\"10142\\\" value=\\\"$NEW_SSAID\\\"|g\" $SSAID_XML'"
echo "    Verify: $(adb shell "su -c \"grep proxima $SSAID_XML\"" 2>/dev/null | sed -n 's/.*value=\"\([^\"]*\)\".*/\1/p' | head -1)"

# ── Global android_id + advertising ID ──

GLOBAL_ID=$(python3 -c "import secrets; print(secrets.token_hex(8))")
echo "[*] Randomizing global android_id → $GLOBAL_ID"
adb shell "settings put secure android_id $GLOBAL_ID"
adb shell "settings delete secure advertising_id" 2>/dev/null || true

# ── Google Services Framework ID (major device fingerprint) ──

echo "[*] Resetting Google Services Framework ID..."
adb shell "su -c 'sqlite3 /data/data/com.google.android.gsf/databases/gservices.db \"DELETE FROM main WHERE name=\\\"android_id\\\"\"'" 2>/dev/null || true
adb shell "su -c 'rm -f /data/data/com.google.android.gsf/databases/gservices.db-journal'" 2>/dev/null || true
adb shell "am force-stop com.google.android.gsf" 2>/dev/null || true
adb shell "am force-stop com.google.android.gms" 2>/dev/null || true

# ── MAC address spoof ──

RAND_MAC=$(python3 -c "
import secrets
b = [secrets.randbelow(256) for _ in range(6)]
b[0] = (b[0] & 0xFC) | 0x02  # locally administered, unicast
print(':'.join(f'{x:02x}' for x in b))
")
echo "[*] Spoofing MAC address → $RAND_MAC"
adb shell "su -c 'ip link set wlan0 down; ip link set wlan0 address $RAND_MAC; ip link set wlan0 up'" 2>/dev/null || true

# ── External storage (survives pm clear!) ──

echo "[*] Clearing external storage (login tokens, UE4 saves, INTL data)..."
adb shell "su -c 'rm -rf /sdcard/Android/data/$PKG'" 2>/dev/null || true
adb shell "su -c 'rm -rf /data/media/0/Android/data/$PKG'" 2>/dev/null || true
adb shell "su -c 'rm -rf /sdcard/Android/obb/$PKG'" 2>/dev/null || true

# ── ACE / Tencent tracking files ──

echo "[*] Removing all ACE/Tencent device fingerprint files..."
adb shell "su -c 'rm -rf \
  /data/data/$PKG/files/ace_shell_di.dat \
  /data/data/$PKG/files/ano_tmp \
  /data/data/$PKG/files/datastore \
  /data/data/$PKG/shared_prefs/tdm.xml \
  /data/data/$PKG/shared_prefs/appsflyer-data.xml \
  /data/data/$PKG/shared_prefs/GCloudCoreSP.xml \
  /data/data/$PKG/shared_prefs/apm_cfg.xml \
  /data/data/$PKG/shared_prefs/tri_cfg.xml \
  /data/data/$PKG/shared_prefs/crashrecord.xml \
  /data/data/$PKG/no_backup \
  /data/data/$PKG/app_crashSight \
  /data/data/$PKG/app_crashrecord'" 2>/dev/null || true

# ── Google Play / Firebase / Facebook tracking ──

echo "[*] Clearing tracking SDK caches..."
adb shell "su -c 'rm -rf \
  /data/data/com.google.android.gms/databases/playlog.db* \
  /data/data/com.google.android.gms/databases/help_feedback* \
  /data/data/com.google.android.gms/databases/icing* \
  /data/data/com.google.android.gms/databases/phenotype* \
  /data/data/$PKG/shared_prefs/com.google.* \
  /data/data/$PKG/shared_prefs/com.facebook.* \
  /data/data/$PKG/shared_prefs/Firebase*'" 2>/dev/null || true

# ── UEDumper artifacts (fingerprints tool usage) ──

echo "[*] Cleaning UEDumper artifacts..."
adb shell "su -c 'rm -rf /data/media/0/Download/UEDump3r'" 2>/dev/null || true
adb shell "su -c 'rm -rf /sdcard/Download/UEDump3r'" 2>/dev/null || true

# ── Magisk boot script for persistent prop spoofing ──

echo "[*] Installing Magisk service.d boot script for prop persistence..."
adb shell "su -c 'mkdir -p /data/adb/service.d'"
adb shell "su -c 'cat > /data/adb/service.d/spoof_props.sh << \"BOOTEOF\"
#!/system/bin/sh
# Spoof hardware identifiers on every boot (before any app reads them)
sleep 5
SERIAL=\$(cat /data/local/tmp/.spoofed_serial 2>/dev/null)
FINGERPRINT=\$(cat /data/local/tmp/.spoofed_fingerprint 2>/dev/null)
BUILD_ID=\$(cat /data/local/tmp/.spoofed_build_id 2>/dev/null)
[ -n \"\$SERIAL\" ] && resetprop ro.serialno \"\$SERIAL\" && resetprop ro.boot.serialno \"\$SERIAL\"
[ -n \"\$FINGERPRINT\" ] && resetprop ro.build.fingerprint \"\$FINGERPRINT\"
[ -n \"\$BUILD_ID\" ] && resetprop ro.build.display.id \"\$BUILD_ID\"
BOOTEOF
'"
adb shell "su -c 'chmod 755 /data/adb/service.d/spoof_props.sh'"

# Save current spoof values for boot persistence
echo "[*] Saving spoof values for boot persistence..."
adb shell "su -c 'echo $RAND_SERIAL > /data/local/tmp/.spoofed_serial'"
adb shell "su -c 'echo \"OnePlus/OnePlus5/OnePlus5:10/QKQ1.191014.012/${RAND_BUILD}:user/release-keys\" > /data/local/tmp/.spoofed_fingerprint'"
adb shell "su -c 'echo \"QKQ1.191014.012.${RAND_BUILD}\" > /data/local/tmp/.spoofed_build_id'"

# ── Verify ──

echo ""
echo "=== Verification ==="
echo "  Serial:      $(adb shell 'getprop ro.serialno')"
echo "  SSAID:       $(adb shell "su -c \"grep proxima $SSAID_XML\"" 2>/dev/null | sed -n 's/.*value=\"\([^\"]*\)\".*/\1/p' | head -1)"
echo "  Android ID:  $(adb shell 'settings get secure android_id')"
echo "  MAC:         $(adb shell "su -c 'cat /sys/class/net/wlan0/address'" 2>/dev/null)"
echo "  Fingerprint: $(adb shell 'getprop ro.build.fingerprint')"
echo "  GSF ID:      $(adb shell "su -c 'sqlite3 /data/data/com.google.android.gsf/databases/gservices.db \"SELECT value FROM main WHERE name=\\\"android_id\\\"\"'" 2>/dev/null || echo 'cleared')"
echo ""
echo "[+] Full device identity reset complete."
echo "    IMPORTANT: Reboot recommended for GSF ID regeneration: adb reboot"
