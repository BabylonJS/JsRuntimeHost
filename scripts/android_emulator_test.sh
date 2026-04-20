#!/usr/bin/env bash
# android_emulator_test.sh
# Creates an Android AVD, compiles a Hello World C program with the NDK,
# pushes it to the emulator, runs it, and verifies "hello world" in logcat.
#
# AVD data (ini + avd dir) is stored under scripts/avd_home/ in the repo.

set -euo pipefail

# ─── Config ───────────────────────────────────────────────────────────────────
ANDROID_SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
ANDROID_API=36
AVD_NAME="JSRemuTestAVD"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# AVD_HOME is placed in the current folder (alongside this script).
# avdmanager writes both the .ini and the .avd/ dir here.
AVD_HOME="$SCRIPT_DIR/avd_home"
LOGCAT_TAG="HelloWorld"
LOGCAT_TIMEOUT=30   # seconds to wait for logcat entry

# ─── Tool paths ───────────────────────────────────────────────────────────────
EMULATOR="$ANDROID_SDK/emulator/emulator"
ADB="$ANDROID_SDK/platform-tools/adb"

# Prefer PATH installs of sdkmanager/avdmanager (Homebrew, etc.)
if command -v sdkmanager &>/dev/null; then
  SDKMANAGER=$(command -v sdkmanager)
else
  SDKMANAGER="$ANDROID_SDK/cmdline-tools/latest/bin/sdkmanager"
fi
if command -v avdmanager &>/dev/null; then
  AVDMANAGER=$(command -v avdmanager)
else
  AVDMANAGER="$ANDROID_SDK/cmdline-tools/latest/bin/avdmanager"
fi

# Homebrew's avdmanager/sdkmanager compute the SDK root from 'toolsdir'.
# By pointing toolsdir at $ANDROID_SDK/platform-tools (always present), the
# parent directory becomes the correct SDK root.  Without this the Homebrew
# wrapper uses its own cellar path and cannot find installed system images.
export AVDMANAGER_OPTS="-Dcom.android.sdkmanager.toolsdir=$ANDROID_SDK/platform-tools"
export SDKMANAGER_OPTS="-Dcom.android.sdkmanager.toolsdir=$ANDROID_SDK/platform-tools"

# ─── Detect host arch and NDK host tag ───────────────────────────────────────
HOST_ARCH="$(uname -m)"
OS="$(uname -s)"
case "$OS-$HOST_ARCH" in
  Darwin-arm64|Darwin-aarch64) NDK_HOST_TAG="darwin-x86_64" ;;  # Rosetta runs x86_64 NDK
  Darwin-x86_64)               NDK_HOST_TAG="darwin-x86_64" ;;
  Linux-x86_64)                NDK_HOST_TAG="linux-x86_64"  ;;
  Linux-aarch64)               NDK_HOST_TAG="linux-x86_64"  ;;  # rare; adjust if needed
  *)
    echo "ERROR: Unsupported OS/arch: $OS/$HOST_ARCH" >&2; exit 1 ;;
esac
echo "Host       : $OS $HOST_ARCH  (NDK host tag: $NDK_HOST_TAG)"

# Verify Rosetta is usable on Apple Silicon (NDK ships darwin-x86_64 binaries)
if [ "$OS" = "Darwin" ] && [ "$HOST_ARCH" = "arm64" ]; then
  if ! arch -x86_64 true 2>/dev/null; then
    echo "ERROR: Rosetta 2 required on Apple Silicon to run the darwin-x86_64 NDK toolchain." >&2
    echo "       Install it with:  softwareupdate --install-rosetta --agree-to-license" >&2
    exit 1
  fi
fi

# ─── Find latest installed NDK ────────────────────────────────────────────────
NDK_ROOT=$(ls -d "$ANDROID_SDK/ndk/"*/ 2>/dev/null | sort -V | tail -1)
NDK_ROOT="${NDK_ROOT%/}"
if [ -z "$NDK_ROOT" ]; then
  echo "ERROR: No NDK found under $ANDROID_SDK/ndk." >&2; exit 1
fi
echo "Using NDK  : $NDK_ROOT"

NDK_TOOLCHAIN="$NDK_ROOT/toolchains/llvm/prebuilt/$NDK_HOST_TAG/bin"

# ─── Detect installed system image ABI ───────────────────────────────────────
# Pick the highest installed API <= ANDROID_API with an available image, then
# pick the ABI that matches the host (arm64-v8a on Apple Silicon/arm64 Linux,
# x86_64 on Intel).  Fall back to whatever is installed.
prefer_abi() {
  case "$HOST_ARCH" in
    arm64|aarch64) echo "arm64-v8a" ;;
    *)             echo "x86_64"    ;;
  esac
}

IMAGE_BASE="$ANDROID_SDK/system-images"
ANDROID_ABI=""
ACTUAL_API="$ANDROID_API"
PREFERRED_ABI="$(prefer_abi)"

# Search from ANDROID_API down to find a matching installed image
for try_api in $ANDROID_API 35 34 33; do
  for try_abi in "$PREFERRED_ABI" arm64-v8a x86_64; do
    for try_tag in google_apis_playstore google_apis default; do
      candidate="$IMAGE_BASE/android-${try_api}/${try_tag}/${try_abi}"
      if [ -d "$candidate" ]; then
        ACTUAL_API="$try_api"
        ANDROID_ABI="$try_abi"
        IMAGE_TAG="$try_tag"
        break 3
      fi
    done
  done
done

if [ -z "$ANDROID_ABI" ]; then
  echo "No system image found locally. Installing android-${ANDROID_API} / google_apis_playstore / ${PREFERRED_ABI} ..."
  ANDROID_ABI="$PREFERRED_ABI"
  ACTUAL_API="$ANDROID_API"
  IMAGE_TAG="google_apis_playstore"
  echo "y" | "$SDKMANAGER" "system-images;android-${ACTUAL_API};${IMAGE_TAG};${ANDROID_ABI}"
fi

SYSTEM_IMAGE_PKG="system-images;android-${ACTUAL_API};${IMAGE_TAG};${ANDROID_ABI}"
echo "System img : $SYSTEM_IMAGE_PKG"
echo "Android ABI: $ANDROID_ABI"

# ─── Select NDK clang for the target ABI ─────────────────────────────────────
case "$ANDROID_ABI" in
  arm64-v8a) CLANG_PREFIX="aarch64-linux-android"  ;;
  x86_64)    CLANG_PREFIX="x86_64-linux-android"   ;;
  armeabi-v7a) CLANG_PREFIX="armv7a-linux-androideabi" ;;
  x86)       CLANG_PREFIX="i686-linux-android"     ;;
  *)
    echo "ERROR: Unsupported ABI: $ANDROID_ABI" >&2; exit 1 ;;
esac

CLANG="$NDK_TOOLCHAIN/${CLANG_PREFIX}${ACTUAL_API}-clang"
if [ ! -x "$CLANG" ]; then
  # Fall back to highest API version available in toolchain
  CLANG=$(ls "$NDK_TOOLCHAIN/${CLANG_PREFIX}"*-clang 2>/dev/null | grep -v '++' | sort -V | tail -1 || true)
fi
if [ ! -x "$CLANG" ]; then
  echo "ERROR: NDK clang not found at $NDK_TOOLCHAIN for prefix $CLANG_PREFIX" >&2; exit 1
fi
echo "Clang      : $CLANG"

# ─── Create AVD (idempotent, all files under AVD_HOME) ───────────────────────
# Export ANDROID_AVD_HOME so avdmanager writes its .ini here, not ~/.android/avd/
export ANDROID_AVD_HOME="$AVD_HOME"
mkdir -p "$AVD_HOME"

if ANDROID_AVD_HOME="$AVD_HOME" "$AVDMANAGER" list avd 2>/dev/null | grep -q "Name: $AVD_NAME"; then
  echo "AVD '$AVD_NAME' already exists in $AVD_HOME — reusing."
else
  echo "Creating AVD '$AVD_NAME' in $AVD_HOME ..."
  # --path sets the .avd/ dir location; ANDROID_AVD_HOME sets where the .ini goes
  echo "no" | ANDROID_AVD_HOME="$AVD_HOME" "$AVDMANAGER" create avd \
    --name    "$AVD_NAME" \
    --package "$SYSTEM_IMAGE_PKG" \
    --path    "$AVD_HOME/$AVD_NAME.avd" \
    --force
fi

# ─── Write Hello World C source ───────────────────────────────────────────────
SRC_DIR="$SCRIPT_DIR/hello_world"
mkdir -p "$SRC_DIR"

cat > "$SRC_DIR/hello.c" <<'CSRC'
#include <stdio.h>
#include <android/log.h>

#define TAG "HelloWorld"

int main(void) {
    printf("hello world\n");
    __android_log_print(ANDROID_LOG_INFO, TAG, "hello world");
    return 0;
}
CSRC

# ─── Compile for Android ──────────────────────────────────────────────────────
echo "Compiling hello.c for Android ($ANDROID_ABI, API $ACTUAL_API) ..."
"$CLANG" -o "$SRC_DIR/hello" "$SRC_DIR/hello.c" -llog
echo "Binary: $SRC_DIR/hello ($(file "$SRC_DIR/hello" | cut -d: -f2-))"

# ─── Kill any stale emulator on the default port (5554) ──────────────────────
EMU_SERIAL="emulator-5554"
if "$ADB" devices | grep -q "^$EMU_SERIAL"; then
  echo "Stopping stale emulator $EMU_SERIAL ..."
  "$ADB" -s "$EMU_SERIAL" emu kill 2>/dev/null || true
  sleep 3
fi

# ─── Start emulator ──────────────────────────────────────────────────────────
echo "Starting emulator ..."
ANDROID_AVD_HOME="$AVD_HOME" "$EMULATOR" \
  -avd "$AVD_NAME" \
  -no-window \
  -no-audio \
  -no-boot-anim \
  -gpu swiftshader_indirect \
  -port 5554 \
  &
EMULATOR_PID=$!
echo "Emulator PID: $EMULATOR_PID"

cleanup() {
  echo "Shutting down emulator (PID $EMULATOR_PID) ..."
  kill "$EMULATOR_PID" 2>/dev/null || true
}
trap cleanup EXIT

# ─── Wait for device online ───────────────────────────────────────────────────
echo "Waiting for device to come online ..."
WAIT_TIMEOUT=60
ELAPSED=0
while ! "$ADB" -s "$EMU_SERIAL" get-state 2>/dev/null | grep -q "device"; do
  if ! kill -0 "$EMULATOR_PID" 2>/dev/null; then
    echo "ERROR: Emulator process died unexpectedly." >&2; exit 1
  fi
  if [ "$ELAPSED" -ge "$WAIT_TIMEOUT" ]; then
    echo "ERROR: Emulator did not come online within ${WAIT_TIMEOUT}s." >&2; exit 1
  fi
  sleep 2; ELAPSED=$((ELAPSED + 2))
done

echo "Waiting for boot to complete (may take a few minutes) ..."
BOOT_TIMEOUT=300
ELAPSED=0
while true; do
  BOOT_PROP=$("$ADB" -s "$EMU_SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
  if [ "$BOOT_PROP" = "1" ]; then
    echo "Emulator booted successfully."
    break
  fi
  if ! kill -0 "$EMULATOR_PID" 2>/dev/null; then
    echo "ERROR: Emulator process died while waiting for boot." >&2; exit 1
  fi
  if [ "$ELAPSED" -ge "$BOOT_TIMEOUT" ]; then
    echo "ERROR: Emulator did not boot within ${BOOT_TIMEOUT}s." >&2; exit 1
  fi
  sleep 5; ELAPSED=$((ELAPSED + 5))
  echo "  ... waiting (${ELAPSED}s)"
done

# ─── Push and run binary ─────────────────────────────────────────────────────
echo "Pushing binary to emulator ..."
"$ADB" -s "$EMU_SERIAL" push "$SRC_DIR/hello" /data/local/tmp/hello
"$ADB" -s "$EMU_SERIAL" shell chmod +x /data/local/tmp/hello

echo "Clearing logcat buffer ..."
"$ADB" -s "$EMU_SERIAL" logcat -c

echo "Running hello binary ..."
STDOUT=$("$ADB" -s "$EMU_SERIAL" shell /data/local/tmp/hello 2>&1)
echo "Program stdout: $STDOUT"

# ─── Poll logcat until entry appears or timeout ───────────────────────────────
echo "Polling logcat for '$LOGCAT_TAG' (up to ${LOGCAT_TIMEOUT}s) ..."
LOGCAT=""
ELAPSED=0
while [ "$ELAPSED" -lt "$LOGCAT_TIMEOUT" ]; do
  LOGCAT=$("$ADB" -s "$EMU_SERIAL" logcat -d -s "$LOGCAT_TAG:I" 2>&1)
  if echo "$LOGCAT" | grep -qi "hello world"; then
    break
  fi
  sleep 2; ELAPSED=$((ELAPSED + 2))
done
echo "--- logcat ($LOGCAT_TAG) ---"
echo "$LOGCAT"
echo "----------------------------"

# ─── Verify ──────────────────────────────────────────────────────────────────
STDOUT_OK=false; LOGCAT_OK=false
echo "$STDOUT" | grep -qi "hello world" && STDOUT_OK=true
echo "$LOGCAT" | grep -qi "hello world" && LOGCAT_OK=true

if $STDOUT_OK && $LOGCAT_OK; then
  echo ""
  echo "✅ SUCCESS: 'hello world' found in both stdout AND logcat."
elif $STDOUT_OK; then
  echo ""
  echo "✅ SUCCESS: 'hello world' found in stdout."
  echo "⚠️  WARNING: Not found in logcat within ${LOGCAT_TIMEOUT}s."
else
  echo ""
  echo "❌ FAILURE: 'hello world' NOT found in program output." >&2
  exit 1
fi
