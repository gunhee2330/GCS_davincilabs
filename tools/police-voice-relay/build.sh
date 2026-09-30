#!/usr/bin/env bash
# Builds DAVINCI_VoiceRelay.apk with the Android SDK command-line tools alone, no Gradle, and
# signs it with the debug keystore the station's test builds use.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
sdk="${ANDROID_HOME:-/c/Users/USER/AppData/Local/Android/Sdk}"
tools="$sdk/build-tools/36.0.0"
platform="$sdk/platforms/android-36/android.jar"
jdk="${JAVA_HOME:-$(ls -d "/c/Program Files/Eclipse Adoptium/"*/ | head -1)}"
keystore="${KEYSTORE:-$here/../../build/Android/qgc-android-debug.keystore}"
version_code="${VERSION_CODE:-2}"
out="$here/build"

rm -rf "$out"
mkdir -p "$out/classes" "$out/dex"

"$jdk/bin/javac" --release 11 -encoding UTF-8 -classpath "$platform" -d "$out/classes" \
    $(find "$here/src" -name '*.java')
JAVA_HOME="$jdk" "$tools/d8.bat" --release --min-api 28 --lib "$platform" --output "$out/dex" \
    $(find "$out/classes" -name '*.class')
"$tools/aapt2.exe" link -o "$out/unsigned.apk" -I "$platform" --manifest "$here/AndroidManifest.xml" \
    --min-sdk-version 28 --target-sdk-version 33 --version-code "$version_code" --version-name "1.$version_code"
python - "$out/unsigned.apk" "$out/dex/classes.dex" <<'EOF'
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1], 'a', zipfile.ZIP_DEFLATED) as apk:
    apk.write(sys.argv[2], 'classes.dex')
EOF
"$tools/zipalign.exe" -p -f 4 "$out/unsigned.apk" "$out/aligned.apk"
JAVA_HOME="$jdk" "$tools/apksigner.bat" sign --ks "$keystore" --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android --out "$out/DAVINCI_VoiceRelay.apk" "$out/aligned.apk"
echo "$out/DAVINCI_VoiceRelay.apk"

# A sideloaded app stays in the stopped state, deaf to BOOT_COMPLETED, until it is opened once.
# The battery exemption is what lets Android restart the relay in the background after a kill.
cat <<EOF

Install on a handset:
  adb install -r DAVINCI_VoiceRelay.apk
  adb shell pm grant org.mavlink.qgroundcontrol.voicerelay android.permission.POST_NOTIFICATIONS
  adb shell dumpsys deviceidle whitelist +org.mavlink.qgroundcontrol.voicerelay
  adb shell am start -n org.mavlink.qgroundcontrol.voicerelay/.MainActivity
Keep the app out of Tailscale's App split tunneling list; exclude only the ground station apps.
EOF
