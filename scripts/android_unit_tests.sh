#!/usr/bin/env bash
# android_unit_tests.sh
# Builds the UnitTests Android project with gradlew, runs them on an emulator,
# and reports pass/fail.  Reuses the AVD created by android_emulator_test.sh.

set -euo pipefail

# ─── Config ───────────────────────────────────────────────────────────────────
ANDROID_SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
ANDROID_API=36
AVD_NAME="JSRemuTestAVD"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TESTS_DIR="$REPO_ROOT/Tests"
ANDROID_PROJECT="$TESTS_DIR/UnitTests/Android"
GRADLEW="$ANDROID_PROJECT/gradlew"
AVD_HOME="$SCRIPT_DIR/avd_home"
EMU_SERIAL="emulator-5554"

# ─── Tool paths ───────────────────────────────────────────────────────────────
EMULATOR="$ANDROID_SDK/emulator/emulator"
ADB="$ANDROID_SDK/platform-tools/adb"

if command -v avdmanager &>/dev/null; then
  AVDMANAGER=$(command -v avdmanager)
else
  AVDMANAGER="$ANDROID_SDK/cmdline-tools/latest/bin/avdmanager"
fi

# Homebrew avdmanager needs toolsdir to find the SDK root correctly
export AVDMANAGER_OPTS="-Dcom.android.sdkmanager.toolsdir=$ANDROID_SDK/platform-tools"
export ANDROID_AVD_HOME="$AVD_HOME"

# ─── Detect ABI and NDK version ──────────────────────────────────────────────
HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
  arm64|aarch64) ANDROID_ABI="arm64-v8a" ;;
  x86_64)        ANDROID_ABI="x86_64"    ;;
  *) echo "ERROR: Unsupported arch: $HOST_ARCH" >&2; exit 1 ;;
esac
echo "Host       : $(uname -s) $HOST_ARCH  →  Android ABI: $ANDROID_ABI"

# Auto-detect the highest installed NDK version to override the one in build.gradle
NDK_DIR="$ANDROID_SDK/ndk"
NDK_VERSION=""
if [ -d "$NDK_DIR" ]; then
  NDK_VERSION=$(ls "$NDK_DIR" | sort -V | tail -1)
fi
if [ -z "$NDK_VERSION" ]; then
  echo "ERROR: No NDK found in $NDK_DIR" >&2; exit 1
fi
echo "NDK        : $NDK_VERSION"

# ─── Write local.properties so Gradle can find the SDK ───────────────────────
echo "sdk.dir=$ANDROID_SDK" > "$ANDROID_PROJECT/local.properties"
echo "SDK dir    : $ANDROID_SDK"

# ─── npm install: build the JavaScript test bundle ───────────────────────────
echo ""
echo "═══ Step 1: Building JS test bundle (npm install in $TESTS_DIR) ═══"
(cd "$TESTS_DIR" && npm install)
echo "JS bundle  : $TESTS_DIR/UnitTests/dist/ ✓"

# ─── Ensure AVD exists ───────────────────────────────────────────────────────
echo ""
echo "═══ Step 2: Ensuring AVD exists ═══"
mkdir -p "$AVD_HOME"

SYSTEM_IMAGE_PKG="system-images;android-${ANDROID_API};google_apis_playstore;${ANDROID_ABI}"

if ANDROID_AVD_HOME="$AVD_HOME" "$AVDMANAGER" list avd 2>/dev/null | grep -q "Name: $AVD_NAME"; then
  echo "AVD '$AVD_NAME' already exists — reusing."
else
  echo "Creating AVD '$AVD_NAME' ..."
  echo "no" | ANDROID_AVD_HOME="$AVD_HOME" "$AVDMANAGER" create avd \
    --name    "$AVD_NAME" \
    --package "$SYSTEM_IMAGE_PKG" \
    --path    "$AVD_HOME/$AVD_NAME.avd" \
    --force
fi

# Patch the image path (Homebrew avdmanager writes a spurious "sdk/" prefix)
CONFIG_INI="$AVD_HOME/$AVD_NAME.avd/config.ini"
if grep -q "^image\.sysdir\.1=sdk/" "$CONFIG_INI" 2>/dev/null; then
  sed -i.bak 's|^image\.sysdir\.1=sdk/|image.sysdir.1=|' "$CONFIG_INI"
  echo "Patched config.ini: $(grep '^image\.sysdir\.1=' "$CONFIG_INI")"
fi

# ─── Kill any stale emulator on port 5554 ────────────────────────────────────
if "$ADB" devices | grep -q "^$EMU_SERIAL"; then
  echo "Stopping stale emulator $EMU_SERIAL ..."
  "$ADB" -s "$EMU_SERIAL" emu kill 2>/dev/null || true
  sleep 3
fi

# ─── Start emulator ──────────────────────────────────────────────────────────
echo ""
echo "═══ Step 3: Starting emulator ═══"
ANDROID_AVD_HOME="$AVD_HOME" ANDROID_SDK_ROOT="$ANDROID_SDK" "$EMULATOR" \
  -avd "$AVD_NAME" \
  -no-window \
  -no-audio \
  -no-boot-anim \
  -no-snapshot \
  -gpu swiftshader_indirect \
  -port 5554 \
  &
EMULATOR_PID=$!
echo "Emulator PID: $EMULATOR_PID"

LOGCAT_FILE="$SCRIPT_DIR/logcat.log"
LOGCAT_PID=""

cleanup() {
  echo ""
  [ -n "$LOGCAT_PID" ] && kill "$LOGCAT_PID" 2>/dev/null || true
  [ -f "$LOGCAT_FILE" ] && echo "Logcat saved : $LOGCAT_FILE"
  echo "Shutting down emulator (PID $EMULATOR_PID) ..."
  kill "$EMULATOR_PID" 2>/dev/null || true
}
trap cleanup EXIT

# ─── Wait for device online ───────────────────────────────────────────────────
echo "Waiting for device to come online ..."
WAIT_TIMEOUT=120
ELAPSED=0
while true; do
  DEV_STATE=$("$ADB" -s "$EMU_SERIAL" get-state 2>/dev/null || true)
  [ "$DEV_STATE" = "device" ] && break
  if ! kill -0 "$EMULATOR_PID" 2>/dev/null; then
    echo "ERROR: Emulator process died unexpectedly." >&2; exit 1
  fi
  if [ "$ELAPSED" -ge "$WAIT_TIMEOUT" ]; then
    echo "ERROR: Emulator did not come online within ${WAIT_TIMEOUT}s." >&2; exit 1
  fi
  sleep 2; ELAPSED=$((ELAPSED + 2))
done

echo "Waiting for full boot ..."
BOOT_TIMEOUT=300
ELAPSED=0
while true; do
  BOOT_PROP=$("$ADB" -s "$EMU_SERIAL" shell getprop sys.boot_completed 2>/dev/null || true)
  BOOT_PROP=$(printf '%s' "$BOOT_PROP" | tr -d '\r\n')
  [ "$BOOT_PROP" = "1" ] && { echo "Emulator booted."; break; }
  if ! kill -0 "$EMULATOR_PID" 2>/dev/null; then
    echo "ERROR: Emulator died during boot." >&2; exit 1
  fi
  if [ "$ELAPSED" -ge "$BOOT_TIMEOUT" ]; then
    echo "ERROR: Boot timed out after ${BOOT_TIMEOUT}s." >&2; exit 1
  fi
  sleep 5; ELAPSED=$((ELAPSED + 5))
  echo "  ... waiting (${ELAPSED}s)"
done

# Start logcat capture (clear first so we only capture test output)
"$ADB" -s "$EMU_SERIAL" logcat -c
"$ADB" -s "$EMU_SERIAL" logcat > "$LOGCAT_FILE" 2>&1 &
LOGCAT_PID=$!
echo "Logcat PID : $LOGCAT_PID  →  $LOGCAT_FILE"

# ─── Run Gradle instrumented tests ───────────────────────────────────────────
echo ""
echo "═══ Step 4: Building and running Android unit tests ═══"
echo "Project    : $ANDROID_PROJECT"
echo "Device     : $EMU_SERIAL"
echo ""

GRADLE_EXIT=0
(cd "$ANDROID_PROJECT" && \
  ANDROID_SDK_ROOT="$ANDROID_SDK" \
  "$GRADLEW" connectedDebugAndroidTest \
    -PndkVersion="$NDK_VERSION" \
    2>&1) || GRADLE_EXIT=$?

# ─── Parse test results ───────────────────────────────────────────────────────
echo ""
echo "═══ Step 5: Test results ═══"

RESULTS_DIR="$ANDROID_PROJECT/app/build/outputs/androidTest-results/connected"
RESULT_SUMMARY="$ANDROID_PROJECT/app/build/reports/androidTests/connected/index.html"

TOTAL=0; FAILURES=0; ERRORS=0; SKIPPED=0
if [ -d "$RESULTS_DIR" ]; then
  while IFS= read -r xml; do
    t=$(grep -o 'tests="[0-9]*"'    "$xml" | head -1 | grep -o '[0-9]*' || echo 0)
    f=$(grep -o 'failures="[0-9]*"' "$xml" | head -1 | grep -o '[0-9]*' || echo 0)
    e=$(grep -o 'errors="[0-9]*"'   "$xml" | head -1 | grep -o '[0-9]*' || echo 0)
    s=$(grep -o 'skipped="[0-9]*"'  "$xml" | head -1 | grep -o '[0-9]*' || echo 0)
    TOTAL=$((TOTAL + t))
    FAILURES=$((FAILURES + f))
    ERRORS=$((ERRORS + e))
    SKIPPED=$((SKIPPED + s))
    echo "  Result file: $(basename "$xml") — tests=$t failures=$f errors=$e"
    grep -A5 '<failure' "$xml" 2>/dev/null | head -30 || true
  done < <(find "$RESULTS_DIR" -name "*.xml" 2>/dev/null)
else
  echo "  No result XML files found in $RESULTS_DIR"
fi

echo ""
if [ "$GRADLE_EXIT" -eq 0 ] && [ "$FAILURES" -eq 0 ] && [ "$ERRORS" -eq 0 ]; then
  echo "✅ SUCCESS: All $TOTAL tests passed (skipped: $SKIPPED)."
else
  echo "❌ FAILURE: $FAILURES failure(s), $ERRORS error(s) out of $TOTAL tests." >&2
  [ "$GRADLE_EXIT" -ne 0 ] && echo "   Gradle exit code: $GRADLE_EXIT" >&2
  echo ""
  echo "── Logcat (StdoutLogger / test output) ──────────────────────────────"
  grep "StdoutLogger\|TestRunner\|FAILED\|PASSED\|AndroidRuntime\|FATAL" \
    "$LOGCAT_FILE" 2>/dev/null | tail -60 || true
  echo "── Full logcat: $LOGCAT_FILE ──────────────────────────────────────"
  exit 1
fi
echo "── Full logcat: $LOGCAT_FILE"
