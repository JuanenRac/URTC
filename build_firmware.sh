#!/usr/bin/env bash
# =============================================================================
# build_firmware.sh - Install tools, verify everything, compile all 4 URTC
# firmware binaries (main board app/bootloader, expansion slave app/bootloader)
# from a clean checkout.
#
# PROJECT: URTC (Universal Robot Tool Controller)
# AUTHOR: JuanenRac (Electro Hobby 3D) - electrohobby3d@gmail.com
# LICENSE: GPL-3.0 (same as the firmware this builds - see LICENSE at repo root)
#
# Every step this script performs is documented in full, with the reasoning
# behind each choice, in docs/COMPILE_STM32F303.TXT - read that first if
# anything here needs adjusting. This script automates that document; it
# doesn't replace it.
#
# Usage:
#   ./build_firmware.sh              build all 4 binaries
#   ./build_firmware.sh --clean      wipe the local build/ cache first
#   ./build_firmware.sh master       build only the main board (app+bootloader)
#   ./build_firmware.sh slave        build only the expansion slave (app+bootloader)
# =============================================================================
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
FIRMWARE_OUT="$ROOT/firmware"

# Pinned to STM32CubeF3 v1.11.6's own known-good submodule combination -
# see docs/COMPILE_STM32F303.TXT for why these are pinned rather than
# tracking each repo's own latest master (reproducibility between runs
# of this script, not necessarily a byte-exact match to whatever HAL
# version firmware/*.bin was originally built against - see that same
# document for the real, honest reasoning on why those can differ).
HAL_REPO="https://github.com/STMicroelectronics/stm32f3xx_hal_driver.git"
HAL_COMMIT="953955afe65f89e60e556bbcdba752597f5da65d"
CMSIS_DEVICE_REPO="https://github.com/STMicroelectronics/cmsis_device_f3.git"
CMSIS_DEVICE_TAG="v2.3.8"
CMSIS_CORE_REPO="https://github.com/STMicroelectronics/cmsis_core.git"

PASS=0; WARN=0; FAIL=0
pass() { echo "  OK   $1"; PASS=$((PASS+1)); }
warn() { echo "  WARN $1"; WARN=$((WARN+1)); }
fail() { echo "  FAIL $1"; FAIL=$((FAIL+1)); }
step() { echo ""; echo "=== $1 ==="; }

TARGET="${1:-all}"
if [ "$1" = "--clean" ]; then
    echo "Removing $BUILD ..."
    rm -rf "$BUILD"
    TARGET="${2:-all}"
fi

# -----------------------------------------------------------------------
step "1. Toolchain"
# -----------------------------------------------------------------------
if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    echo "arm-none-eabi-gcc not found - attempting install via apt..."
    if command -v apt >/dev/null 2>&1; then
        sudo apt update
        sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi \
            libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
            libstdc++-arm-none-eabi-dev git
    else
        fail "no apt available and arm-none-eabi-gcc is missing - install the ARM GNU Toolchain manually (see docs/COMPILE_STM32F303.TXT section 2), then re-run this script"
        echo ""; echo "$PASS passed, $WARN warnings, $FAIL failed"; exit 1
    fi
fi
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    pass "arm-none-eabi-gcc found: $(arm-none-eabi-gcc --version | head -1)"
else
    fail "arm-none-eabi-gcc still not found after install attempt"
    echo ""; echo "$PASS passed, $WARN warnings, $FAIL failed"; exit 1
fi
for tool in arm-none-eabi-g++ arm-none-eabi-objcopy arm-none-eabi-size arm-none-eabi-nm; do
    if command -v $tool >/dev/null 2>&1; then
        pass "$tool found"
    else
        fail "$tool not found - the gcc-arm-none-eabi package should provide this; check your install"
    fi
done
if command -v git >/dev/null 2>&1; then
    pass "git found: $(git --version)"
else
    fail "git not found - needed to fetch ST's own HAL/CMSIS sources. Install it (apt install git) and re-run."
    echo ""; echo "$PASS passed, $WARN warnings, $FAIL failed"; exit 1
fi

# -----------------------------------------------------------------------
step "2. ST HAL/CMSIS sources (cached locally under build/vendor/ after first run)"
# -----------------------------------------------------------------------
mkdir -p "$BUILD/vendor" "$BUILD/common/HAL_Include" "$BUILD/common/CMSIS_Include" "$BUILD/hal_src" "$BUILD/hal_obj"

if [ ! -d "$BUILD/vendor/hal" ]; then
    echo "Fetching STM32F3xx HAL driver (pinned commit)..."
    mkdir -p "$BUILD/vendor/hal"
    (cd "$BUILD/vendor/hal" && git -c safe.directory='*' init -q && git -c safe.directory='*' remote add origin "$HAL_REPO" \
        && git -c safe.directory='*' fetch --depth 1 origin "$HAL_COMMIT" -q && git -c safe.directory='*' checkout -q FETCH_HEAD)
    pass "HAL driver fetched"
else
    pass "HAL driver already cached at build/vendor/hal"
fi

if [ ! -d "$BUILD/vendor/cmsis_device_f3" ]; then
    echo "Fetching CMSIS device headers for F3 (pinned tag $CMSIS_DEVICE_TAG)..."
    git -c safe.directory='*' clone --depth 1 --branch "$CMSIS_DEVICE_TAG" -q "$CMSIS_DEVICE_REPO" "$BUILD/vendor/cmsis_device_f3"
    pass "CMSIS device (F3) fetched"
else
    pass "CMSIS device (F3) already cached at build/vendor/cmsis_device_f3"
fi

if [ ! -d "$BUILD/vendor/cmsis_core" ]; then
    echo "Fetching generic ARM CMSIS Core headers (Include/ only, sparse)..."
    git -c safe.directory='*' clone --depth 1 --filter=blob:none --no-checkout -q "$CMSIS_CORE_REPO" "$BUILD/vendor/cmsis_core"
    (cd "$BUILD/vendor/cmsis_core" && git -c safe.directory='*' sparse-checkout init --no-cone \
        && echo "/CMSIS/Core/Include/**" > .git/info/sparse-checkout \
        && git -c safe.directory='*' checkout -q)
    pass "CMSIS core fetched (Include/ only)"
else
    pass "CMSIS core already cached at build/vendor/cmsis_core"
fi

# Assemble the flat include/source tree every build below compiles against.
cp "$BUILD/vendor/hal/Inc/"*.h "$BUILD/common/HAL_Include/" 2>/dev/null || true
cp -r "$BUILD/vendor/hal/Inc/Legacy" "$BUILD/common/HAL_Include/" 2>/dev/null || true
cp "$BUILD/common/HAL_Include/stm32f3xx_hal_conf_template.h" "$BUILD/common/HAL_Include/stm32f3xx_hal_conf.h"
cp "$BUILD/vendor/hal/Src/"*.c "$BUILD/hal_src/"
cp "$BUILD/vendor/cmsis_device_f3/Include/"*.h "$BUILD/common/CMSIS_Include/"
cp -r "$BUILD/vendor/cmsis_core/CMSIS/Core/Include/"* "$BUILD/common/CMSIS_Include/"
if [ -f "$BUILD/common/CMSIS_Include/core_cm4.h" ] && [ -f "$BUILD/common/HAL_Include/stm32f3xx_hal.h" ]; then
    pass "HAL/CMSIS include tree assembled"
else
    fail "HAL/CMSIS include tree incomplete - check build/common/ manually"
fi

# -----------------------------------------------------------------------
step "3. Common compiler flags and shared HAL objects (21 modules, shared by all 4 builds)"
# -----------------------------------------------------------------------
CFLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DSTM32F303xC -DUSE_HAL_DRIVER -I$BUILD/common/CMSIS_Include -I$BUILD/common/HAL_Include -O2 -Wall -ffunction-sections -fdata-sections"
CXXFLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics -specs=nano.specs -specs=nosys.specs -O2 -Wall"
LDCOMMON="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections"

HAL_MODULES="stm32f3xx_hal stm32f3xx_hal_adc stm32f3xx_hal_adc_ex stm32f3xx_hal_can stm32f3xx_hal_cortex stm32f3xx_hal_dac stm32f3xx_hal_dac_ex stm32f3xx_hal_dma stm32f3xx_hal_flash stm32f3xx_hal_flash_ex stm32f3xx_hal_gpio stm32f3xx_hal_i2c stm32f3xx_hal_i2c_ex stm32f3xx_hal_iwdg stm32f3xx_hal_pwr stm32f3xx_hal_pwr_ex stm32f3xx_hal_rcc stm32f3xx_hal_rcc_ex stm32f3xx_hal_spi stm32f3xx_hal_tim stm32f3xx_hal_tim_ex"

HAL_OK=1
for f in $HAL_MODULES; do
    if [ ! -f "$BUILD/hal_obj/$f.o" ] || [ "$BUILD/hal_src/$f.c" -nt "$BUILD/hal_obj/$f.o" ]; then
        arm-none-eabi-gcc $CFLAGS -x c -c "$BUILD/hal_src/$f.c" -o "$BUILD/hal_obj/$f.o" || HAL_OK=0
    fi
done
if [ "$HAL_OK" = "1" ] && [ "$(ls "$BUILD/hal_obj"/*.o 2>/dev/null | wc -l)" = "21" ]; then
    pass "21/21 HAL modules compiled"
else
    fail "one or more HAL modules failed to compile - see errors above"
    echo ""; echo "$PASS passed, $WARN warnings, $FAIL failed"; exit 1
fi

# Startup + system files, once per chip (identical source, kept as separate
# object sets so a main-board object can never accidentally end up linked
# into a slave build or vice versa - see docs/COMPILE_STM32F303.TXT section 4).
for chip in app_master app_slave; do
    mkdir -p "$BUILD/$chip"
    cp "$BUILD/vendor/cmsis_device_f3/Source/Templates/gcc/startup_stm32f303xc.s" "$BUILD/$chip/"
    cp "$BUILD/vendor/cmsis_device_f3/Source/Templates/system_stm32f3xx.c" "$BUILD/$chip/"
    arm-none-eabi-gcc $CFLAGS -x assembler-with-cpp -c "$BUILD/$chip/startup_stm32f303xc.s" -o "$BUILD/$chip/startup.o"
    arm-none-eabi-gcc $CFLAGS -x c -c "$BUILD/$chip/system_stm32f3xx.c" -o "$BUILD/$chip/system_stm32f3xx.o"
done
pass "startup + system files compiled for both chips"

# -----------------------------------------------------------------------
# Helper: compile every .c in a project source folder (flat output dir,
# no filename prefixes - see docs/COMPILE_STM32F303.TXT section 7 on why
# this matters for link-order reproducibility between runs).
# -----------------------------------------------------------------------
compile_dir() {
    local srcdir="$1" outdir="$2" extra_inc="$3"
    for f in "$srcdir"/*.c; do
        [ -e "$f" ] || continue
        arm-none-eabi-gcc $CFLAGS -I"$srcdir" $extra_inc -x c -c "$f" -o "$outdir/$(basename "$f" .c).o"
    done
}

build_bin_hex() {
    local elf="$1"
    arm-none-eabi-objcopy -O binary "$elf" "${elf%.elf}.bin"
    arm-none-eabi-objcopy -O ihex "$elf" "${elf%.elf}.hex"
}

mkdir -p "$FIRMWARE_OUT"

# -----------------------------------------------------------------------
if [ "$TARGET" = "all" ] || [ "$TARGET" = "master" ]; then
step "4. Main board bootloader (src/F303-master/boot/)"
# -----------------------------------------------------------------------
OUT="$BUILD/master_boot"; rm -rf "$OUT"; mkdir -p "$OUT"
SRC="$ROOT/src/F303-master/boot"
compile_dir "$SRC" "$OUT" ""
arm-none-eabi-gcc $LDCOMMON -T"$SRC/STM32F303CCTx_BOOTLOADER.ld" \
    "$BUILD/app_master/startup.o" "$BUILD/app_master/system_stm32f3xx.o" \
    "$OUT"/*.o "$BUILD/hal_obj"/*.o -o "$OUT/URTC_BOOTLOADER.elf" 2>&1 | grep -v "not implemented\|note: the message\|in function \`_" || true
build_bin_hex "$OUT/URTC_BOOTLOADER.elf"
cp "$OUT/URTC_BOOTLOADER.elf" "$OUT/URTC_BOOTLOADER.bin" "$OUT/URTC_BOOTLOADER.hex" "$FIRMWARE_OUT/"
pass "URTC_BOOTLOADER.bin/.hex/.elf built ($(arm-none-eabi-size "$OUT/URTC_BOOTLOADER.elf" | tail -1 | awk '{print $1}') bytes text)"

# -----------------------------------------------------------------------
step "5. Main board application (src/F303-master/)"
# -----------------------------------------------------------------------
OUT="$BUILD/master_app"; rm -rf "$OUT"; mkdir -p "$OUT"
SRC="$ROOT/src/F303-master"
compile_dir "$SRC" "$OUT" "-I$SRC/melexis_mlx90640 -I$SRC/melexis_mlx90641 -I$SRC/melexis_mlx90642"
compile_dir "$SRC/melexis_mlx90640" "$OUT" "-I$SRC -I$SRC/melexis_mlx90640"
compile_dir "$SRC/melexis_mlx90641" "$OUT" "-I$SRC -I$SRC/melexis_mlx90641"
compile_dir "$SRC/melexis_mlx90642" "$OUT" "-I$SRC -I$SRC/melexis_mlx90642"
# The one C++ file in the whole project - see docs/COMPILE_STM32F303.TXT section 6
arm-none-eabi-g++ $CXXFLAGS -I"$SRC/melexis_mlx90641" \
    -c "$SRC/melexis_mlx90641/MLX90641_API.cpp" -o "$OUT/MLX90641_API.o"
arm-none-eabi-g++ $LDCOMMON -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics \
    -T"$SRC/STM32F303CCTx_APP.ld" \
    "$BUILD/app_master/startup.o" "$BUILD/app_master/system_stm32f3xx.o" \
    "$OUT"/*.o "$BUILD/hal_obj"/*.o -o "$OUT/URTC_APP.elf" 2>&1 | grep -v "not implemented\|note: the message\|in function \`_" || true
build_bin_hex "$OUT/URTC_APP.elf"
# Filename encodes the firmware version - read it from firmware_common.h
FW_VER=$(sed -n 's/^#define FIRMWARE_VERSION_MAJOR[[:space:]]\+\([0-9]\+\).*/\1/p' "$SRC/firmware_common.h")
FW_MIN=$(sed -n 's/^#define FIRMWARE_VERSION_MINOR[[:space:]]\+\([0-9]\+\).*/\1/p' "$SRC/firmware_common.h")
[ -z "$FW_VER" ] && FW_VER="?"
[ -z "$FW_MIN" ] && FW_MIN="?"
APP_NAME="URTC_V${FW_VER}.${FW_MIN}_F303CC"
cp "$OUT/URTC_APP.elf" "$FIRMWARE_OUT/${APP_NAME}.elf"
cp "$OUT/URTC_APP.bin" "$FIRMWARE_OUT/${APP_NAME}.bin"
cp "$OUT/URTC_APP.hex" "$FIRMWARE_OUT/${APP_NAME}.hex"
MLX_PUBLIC_OK=$(arm-none-eabi-nm "$OUT/URTC_APP.elf" | grep -cE " T MLX90641_(DumpEE|ExtractParameters|GetFrameData|GetTa|CalculateTo)\b" || true)
if [ "$MLX_PUBLIC_OK" -ge 5 ]; then
    pass "${APP_NAME}.bin/.hex/.elf built, MLX90641's own public C API confirmed unmangled (extern \"C\" wrapper working)"
else
    warn "${APP_NAME}.bin built, but MLX90641's own public functions weren't all found unmangled - check the extern \"C\" wrapper in MLX90641_API.h (note: Melexis's own internal-only _Z-prefixed helper symbols, e.g. HammingDecode/ExtractCPParameters, are normal and expected here - they're never called from C, only from other C++ code in that same file)"
fi
fi

# -----------------------------------------------------------------------
if [ "$TARGET" = "all" ] || [ "$TARGET" = "slave" ]; then
step "6. Expansion slave bootloader (src/F303-slave/boot/)"
# -----------------------------------------------------------------------
OUT="$BUILD/slave_boot"; rm -rf "$OUT"; mkdir -p "$OUT"
SRC="$ROOT/src/F303-slave/boot"
compile_dir "$SRC" "$OUT" ""
arm-none-eabi-gcc $LDCOMMON -T"$SRC/STM32F303CBTx_SLAVEBOOT.ld" \
    "$BUILD/app_slave/startup.o" "$BUILD/app_slave/system_stm32f3xx.o" \
    "$OUT"/*.o "$BUILD/hal_obj"/*.o -o "$OUT/URTC_SLAVE_BOOTLOADER.elf" 2>&1 | grep -v "not implemented\|note: the message\|in function \`_" || true
build_bin_hex "$OUT/URTC_SLAVE_BOOTLOADER.elf"
cp "$OUT/URTC_SLAVE_BOOTLOADER.elf" "$OUT/URTC_SLAVE_BOOTLOADER.bin" "$OUT/URTC_SLAVE_BOOTLOADER.hex" "$FIRMWARE_OUT/"
pass "URTC_SLAVE_BOOTLOADER.bin/.hex/.elf built ($(arm-none-eabi-size "$OUT/URTC_SLAVE_BOOTLOADER.elf" | tail -1 | awk '{print $1}') bytes text)"

# -----------------------------------------------------------------------
step "7. Expansion slave application (src/F303-slave/)"
# -----------------------------------------------------------------------
OUT="$BUILD/slave_app"; rm -rf "$OUT"; mkdir -p "$OUT"
SRC="$ROOT/src/F303-slave"
compile_dir "$SRC" "$OUT" "-I$SRC/melexis_mlx90640 -I$SRC/melexis_mlx90641 -I$SRC/melexis_mlx90642"
compile_dir "$SRC/melexis_mlx90640" "$OUT" "-I$SRC -I$SRC/melexis_mlx90640 -I$SRC/melexis_mlx90641 -I$SRC/melexis_mlx90642"
arm-none-eabi-g++ $CXXFLAGS -I"$SRC/melexis_mlx90641" \
    -c "$SRC/melexis_mlx90641/MLX90641_API.cpp" -o "$OUT/MLX90641_API.o"
compile_dir "$SRC/melexis_mlx90642" "$OUT" "-I$SRC -I$SRC/melexis_mlx90640 -I$SRC/melexis_mlx90641 -I$SRC/melexis_mlx90642"
arm-none-eabi-g++ $LDCOMMON -fno-exceptions -fno-rtti -fno-unwind-tables -fno-threadsafe-statics \
    -T"$SRC/STM32F303CBTx_SLAVEAPP.ld" \
    "$BUILD/app_slave/startup.o" "$BUILD/app_slave/system_stm32f3xx.o" \
    "$OUT"/*.o "$BUILD/hal_obj"/*.o -lm -o "$OUT/URTC_SLAVE_APP.elf" 2>&1 | grep -v "not implemented\|note: the message\|in function \`_" || true
build_bin_hex "$OUT/URTC_SLAVE_APP.elf"
cp "$OUT/URTC_SLAVE_APP.elf" "$OUT/URTC_SLAVE_APP.bin" "$OUT/URTC_SLAVE_APP.hex" "$FIRMWARE_OUT/"
MLX_PUBLIC_OK=$(arm-none-eabi-nm "$OUT/URTC_SLAVE_APP.elf" | grep -cE " T MLX90641_(DumpEE|ExtractParameters|GetFrameData|GetTa|CalculateTo)\b" || true)
if [ "$MLX_PUBLIC_OK" -ge 5 ]; then
    pass "URTC_SLAVE_APP.bin/.hex/.elf built, MLX90641's own public C API confirmed unmangled (extern \"C\" wrapper working)"
else
    warn "URTC_SLAVE_APP.bin built, but MLX90641's own public functions weren't all found unmangled - check the extern \"C\" wrapper in MLX90641_API.h"
fi
fi

# -----------------------------------------------------------------------
if [ "$TARGET" = "all" ] && [ "$FAIL" = "0" ]; then
step "8. Firmware manifest (firmware_manifest.json)"
# -----------------------------------------------------------------------
if python3 "$ROOT/generate_manifest.py" "$ROOT"; then
    pass "firmware_manifest.json regenerated"
else
    warn "firmware_manifest.json regeneration failed - see the traceback above"
fi
fi
if [ "$TARGET" != "all" ]; then
    warn "firmware_manifest.json NOT regenerated - only ran a partial ($TARGET) build. Run without a target argument (or with 'all') to refresh it."
fi

# -----------------------------------------------------------------------
step "Summary"
# -----------------------------------------------------------------------
echo "$PASS passed, $WARN warnings, $FAIL failed"
echo ""
echo "Output binaries are in: $FIRMWARE_OUT/"
echo ""
echo "Note on reproducibility: this script pins a specific, known-good ST"
echo "HAL/CMSIS version combination (see the top of this file) so repeated"
echo "runs of THIS script produce consistent results. That pinned version"
echo "is not guaranteed to be byte-for-byte identical to whatever HAL"
echo "version firmware/*.bin was originally built against, if those files"
echo "already existed before this run - a same-size, different-bytes result"
echo "is expected and not a sign anything is wrong (see"
echo "docs/COMPILE_STM32F303.TXT for the full reasoning). A DIFFERENT file"
echo "size, or a build failure, is the real thing worth investigating."
echo ""
echo "Remember this project's own standing rules before committing any"
echo "change that triggered this rebuild: a bootloader source edit needs"
echo "its own version bump (BOOTLOADER_VERSION_MAJOR/MINOR/PATCH) before"
echo "this rebuild counts as final - see VERSION_CHECKLIST.txt."

if [ "$FAIL" -gt 0 ]; then exit 1; fi
