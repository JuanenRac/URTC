#!/bin/bash
# =============================================================================
# URTC - Version consistency checker
# =============================================================================
# Companion to VERSION_CHECKLIST.txt - read that file first, this script is
# the mechanical half of it. Run from the project root (the folder
# containing README.md, src/, tools/, docs/, etc).
#
# What this does: reads the 4 Track A/E code constants as the source of
# truth (main board firmware, Flasher, Tester, expansion slave app -
# only the main firmware and the 2 PC tools actually move together as
# "the project version"; the slave's own version is reported separately,
# never compared against Track A's number, since Track E is fully
# independent), then greps every location VERSION_CHECKLIST.txt
# documents under Track A/A6 for the version tag this bump should have
# replaced, reporting any that still show the old one. Does NOT touch
# Track B (hardware/PCB) or Track C (main board bootloader) - neither
# moves with this number, checking them here would just create false
# alarms.
#
# Rewritten from scratch (previous version predated, and completely
# assumed, the VX.X/ version-numbered folder scheme and the dual
# monolithic/partitioned source form - both retired project-wide on
# 1 August 2026. The old script's very first check would have failed
# outright ("no src/F303-master/VX.X/ folder found") against the
# current flat, single-form layout - confirmed by actually trying to
# run it before rewriting, not assumed from reading it alone.
#
# Exit code 0 = everything consistent. Non-zero = see the printed report.
# This does not fix anything by itself - it only reports. Fixing still
# needs a human (or Claude) to apply VERSION_CHECKLIST.txt's guidance,
# since some hits need judgment (e.g. mejoras_futuras.txt entries) rather
# than a blind substitution.

set -uo pipefail
ROOT="$(pwd)"
FAIL=0
WARN=0

pass() { echo "  OK   $1"; }
fail() { echo "  FAIL $1"; FAIL=$((FAIL+1)); }
warn() { echo "  WARN $1"; WARN=$((WARN+1)); }

echo "============================================================"
echo "1. Reading Track A source-of-truth constants"
echo "============================================================"

FW_DIR="$ROOT/src/F303-master"
if [ ! -f "$FW_DIR/firmware_common.h" ]; then
    echo "FATAL: $FW_DIR/firmware_common.h not found - is this the project root, and does src/F303-master/ still exist as a flat folder (no VX.X/ subfolder)?"
    exit 2
fi

# sed (POSIX BRE), not grep -oP: PCRE lookbehind needs a UTF-8-aware grep
# build/locale to work at all - on a plain/C locale (confirmed reproducible
# on this exact machine: "grep: -P supports only unibyte and UTF-8 locales")
# grep -P exits nonzero and prints nothing, which the 2>/dev/null here was
# silently swallowing - FW_MINOR came back empty even though the #define
# was right there, cascading into CURRENT=UNKNOWN and skipping sections
# 3-5 entirely. sed's BRE engine has no such locale dependency, and
# build_firmware.sh already reads this exact FIRMWARE_VERSION_MAJOR/MINOR
# pair the same way (see its own FW_VER/FW_MIN) - reusing that proven
# pattern here instead of a second, PCRE-only implementation of the same
# extraction.
FW_MINOR=$(sed -n 's/^#define[[:space:]]\+FIRMWARE_VERSION_MINOR[[:space:]]\+\([0-9]\+\).*/\1/p' "$FW_DIR/firmware_common.h" 2>/dev/null | head -1)
FLASHER_VER=$(sed -n 's/^FLASHER_VERSION = "\([^"]*\)".*/\1/p' "$ROOT/tools/flasher/flasher_config.py" 2>/dev/null | head -1)
TESTER_VER=$(sed -n 's/^TESTER_VERSION = "\([^"]*\)".*/\1/p' "$ROOT/tools/tester/tester_config.py" 2>/dev/null | head -1)

echo "  FIRMWARE_VERSION_MINOR (main board):  ${FW_MINOR:-NOT FOUND}"
echo "  FLASHER_VERSION:                      ${FLASHER_VER:-NOT FOUND}"
echo "  TESTER_VERSION:                       ${TESTER_VER:-NOT FOUND}"

if [ -z "$FW_MINOR" ]; then
    fail "could not read FIRMWARE_VERSION_MINOR from firmware_common.h - check the #define still exists with this exact name"
    CURRENT="UNKNOWN"
else
    pass "FIRMWARE_VERSION_MINOR read successfully"
    CURRENT="1.${FW_MINOR}"
fi

if [ "$CURRENT" != "UNKNOWN" ]; then
    if [ "$FLASHER_VER" != "$CURRENT" ]; then
        warn "FLASHER_VERSION ($FLASHER_VER) != firmware-derived expectation ($CURRENT) - confirm this is intentional, the 2 PC tools don't have to match the firmware's own MAJOR.MINOR digit-for-digit unless a project-wide migration deliberately moved all 3 together, the way the 1.0->1.1 one did"
    fi
    if [ "$TESTER_VER" != "$CURRENT" ]; then
        warn "TESTER_VERSION ($TESTER_VER) != firmware-derived expectation ($CURRENT) - same caveat"
    fi
fi

echo ""
echo "Using CURRENT=$CURRENT for the rest of this check."
echo ""
echo "============================================================"
echo "2. Folder structure (Track A2) - confirming the flat layout is"
echo "   actually flat, no VX.X/ subfolder has reappeared"
echo "============================================================"
for d in "src/F303-master" "src/F303-master/boot" "src/F303-slave" "src/F303-slave/boot" "tools/flasher" "tools/tester"; do
    if [ -d "$ROOT/$d" ]; then
        pass "$d exists"
    else
        fail "$d MISSING"
    fi
    STRAY_VDIRS=$(find "$ROOT/$d" -maxdepth 1 -type d -iname "V[0-9]*.[0-9]*" 2>/dev/null)
    if [ -n "$STRAY_VDIRS" ]; then
        fail "$d has a version-numbered subfolder that shouldn't exist anymore (the VX.X/ scheme was retired project-wide): $STRAY_VDIRS"
    fi
done
STRAY_MONO=$(find "$ROOT/src" -maxdepth 3 \( -iname "STM32F303CC.C" -o -iname "BOOTLOADER.C" -o -iname "SLAVEAPP.C" -o -iname "SLAVEBOOT.C" \) 2>/dev/null)
if [ -n "$STRAY_MONO" ]; then
    fail "monolithic-form file(s) found that shouldn't exist anymore (retired project-wide): $STRAY_MONO"
else
    pass "no leftover monolithic-form files"
fi

echo ""
echo "============================================================"
echo "3. Binary naming (Track A3, C, E)"
echo "============================================================"
if [ "$CURRENT" != "UNKNOWN" ]; then
    EXPECTED_BIN="URTC_V${CURRENT}_F303CC"
    for ext in bin hex elf; do
        f="$ROOT/firmware/${EXPECTED_BIN}.${ext}"
        [ -f "$f" ] && pass "firmware/$(basename "$f")" || fail "firmware/$(basename "$f") MISSING"
    done
    STALE_BINS=$(find "$ROOT/firmware" -maxdepth 1 -iname "URTC_v*_F303CC*" ! -iname "${EXPECTED_BIN}*" 2>/dev/null)
    [ -n "$STALE_BINS" ] && warn "stale-named main-board firmware binaries still present: $STALE_BINS"
fi
for base in "URTC_BOOTLOADER" "URTC_SLAVE_APP" "URTC_SLAVE_BOOTLOADER"; do
    for ext in bin hex elf; do
        f="$ROOT/firmware/${base}.${ext}"
        [ -f "$f" ] && pass "firmware/$(basename "$f")" || fail "firmware/$(basename "$f") MISSING"
    done
done
STALE_SUFFIXED=$(find "$ROOT/firmware" -maxdepth 1 \( -iname "*_partitioned.*" -o -iname "*_monolithic.*" \) 2>/dev/null)
if [ -n "$STALE_SUFFIXED" ]; then
    fail "binaries with the retired _partitioned/_monolithic suffix still present (only one form exists now): $STALE_SUFFIXED"
else
    pass "no leftover _partitioned/_monolithic binaries"
fi

echo ""
echo "============================================================"
echo "4. Documentation identity-tag mentions (Track A6) - reporting any"
echo "   file that still contains the PREVIOUS minor version's tag where"
echo "   this script can safely assume it should have moved"
echo "============================================================"
PREV_MINOR=-1
if [ "$CURRENT" != "UNKNOWN" ]; then
    PREV_MINOR=$((FW_MINOR - 1))
    PREV="1.${PREV_MINOR}"
    if [ "$PREV_MINOR" -ge 0 ]; then
        DOC_FILES=(
            "LICENSE"
            "PROJECT_INDEX.txt"
            "docs/CANBUS.TXT"
            "docs/ECOVIA.TXT"
            "docs/EEPROM.TXT"
            "docs/EXPANSION.TXT"
            "docs/TOOLS.TXT"
            "src/F303-master/README.md"
            "src/F303-master/boot/README.md"
        )
        for f in "${DOC_FILES[@]}"; do
            full="$ROOT/$f"
            [ -f "$full" ] || { warn "$f not found, skipped"; continue; }
            HITS=$(grep -n "URTC v${PREV}\|URTC (v${PREV})\|PROJECT: URTC v${PREV}\|Project:\*\* URTC v${PREV}\|(V${PREV})" "$full" 2>/dev/null | grep -v "!\[URTC v${PREV}\](images/URTC_BOARD.png)")
            if [ -n "$HITS" ]; then
                fail "$f still references v$PREV as a project identity tag:"
                echo "$HITS" | sed 's/^/         /'
            else
                pass "$f"
            fi
        done
        for tool in flasher tester; do
            for lang in "" _spa _ita _fra _deu; do
                f="tools/$tool/README${lang}.md"
                full="$ROOT/$f"
                [ -f "$full" ] || { warn "$f not found, skipped"; continue; }
                HITS=$(grep -n "[Vv]ersion[^:]*:\*\* ${PREV}\b\|[Vv]ersion :\*\* ${PREV}\b" "$full" 2>/dev/null)
                if [ -n "$HITS" ]; then
                    fail "$f still shows version $PREV in its own header line:"
                    echo "$HITS" | sed 's/^/         /'
                else
                    pass "$f"
                fi
            done
        done
    else
        echo "  (skipped - can't derive a previous version below 1.0)"
    fi
else
    echo "  (skipped - CURRENT unknown, see section 1)"
fi

echo ""
echo "============================================================"
echo "5. Baked-in image/asset version text (Track A5) - can only check"
echo "   the SVGs (text-based); banners and app screenshots are pixel"
echo "   data and need a human/Claude visual check, see"
echo "   VERSION_CHECKLIST.txt section A5"
echo "============================================================"
if [ "$CURRENT" != "UNKNOWN" ] && [ "$PREV_MINOR" -ge 0 ]; then
    for f in "tools/flasher/assets/URTC_LOGO_FLASHER.svg" \
             "tools/tester/assets/URTC_LOGO_TESTER.svg" \
             "images/URTC_LOGO_FLASHER.svg" \
             "images/URTC_LOGO_TESTER.svg"; do
        full="$ROOT/$f"
        [ -f "$full" ] || { warn "$f not found, skipped"; continue; }
        if grep -q "v${PREV}\b" "$full" 2>/dev/null; then
            fail "$f still contains v$PREV in its SVG text/comment"
        else
            pass "$f"
        fi
    done
fi
echo "  REMINDER (not auto-checked): re-render both banner PNGs from"
echo "  their SVGs, and re-capture both app-window screenshots by"
echo "  actually launching the apps - see VERSION_CHECKLIST.txt A5."

echo ""
echo "============================================================"
echo "6. Track E (expansion slave chip) - reported, not compared against"
echo "   Track A, since this track never moves in sync with it"
echo "============================================================"
SLAVE_FILES=(
    "firmware/URTC_SLAVE_APP.bin"
    "firmware/URTC_SLAVE_BOOTLOADER.bin"
    "src/F303-slave/slave_common.h"
    "src/F303-slave/boot/slaveboot_common.h"
)
for f in "${SLAVE_FILES[@]}"; do
    [ -f "$ROOT/$f" ] && pass "$f exists" || fail "$f MISSING"
done
# sed, not grep -oP - see section 1's own note above on why (PCRE
# lookbehind's locale dependency, reproducibly broken on this machine).
SLAVE_BOOT_VER=$(sed -n 's/^#define[[:space:]]\+BOOTLOADER_VERSION_MAJOR[[:space:]]\+\([0-9]\+\).*/\1/p' "$ROOT/src/F303-slave/boot/slaveboot_common.h" 2>/dev/null | head -1)
if [ -n "$SLAVE_BOOT_VER" ]; then
    pass "slave bootloader version constant readable (MAJOR=$SLAVE_BOOT_VER)"
else
    warn "could not read slave bootloader version constant - confirm the #define name hasn't changed"
fi
for d in "melexis_mlx90640" "melexis_mlx90641" "melexis_mlx90642"; do
    if [ -d "$ROOT/src/F303-slave/$d" ]; then
        pass "src/F303-slave/$d/ exists"
    else
        warn "src/F303-slave/$d/ missing - if this is intentional (a sensor library removed), ignore; if not, a vendored library may be missing"
    fi
done
for d in "melexis_mlx90640" "melexis_mlx90641" "melexis_mlx90642"; do
    if [ -d "$ROOT/src/F303-master/$d" ]; then
        pass "src/F303-master/$d/ exists"
    else
        warn "src/F303-master/$d/ missing - if this is intentional, ignore; if not, this board's own direct-connection sensor driver may be missing"
    fi
done

echo ""
echo "============================================================"
echo "7. Syntax sanity for both PC tools"
echo "============================================================"
for tool in flasher tester; do
    d="$ROOT/tools/$tool"
    if [ -d "$d" ]; then
        if (cd "$d" && python3 -m py_compile *.py 2>/tmp/pycompile_err.log); then
            pass "$tool: all .py files compile"
            find "$d" -iname "__pycache__" -exec rm -rf {} + 2>/dev/null
        else
            fail "$tool: py_compile errors - see /tmp/pycompile_err.log"
        fi
    fi
done

echo ""
echo "============================================================"
echo "SUMMARY: $FAIL failures, $WARN warnings"
echo "============================================================"
if [ "$FAIL" -gt 0 ]; then
    echo "Read VERSION_CHECKLIST.txt for what each failure category means"
    echo "and how to fix it - this script only reports, it doesn't edit."
    exit 1
fi
exit 0
