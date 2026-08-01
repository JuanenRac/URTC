# =============================================================================
# URTC Flasher - Configuration, language/translation loading, and protocol
# constants shared by every other module in this project
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import sys
import os
import json

# =============================================================================
# Protocol constants - must match BOOTLOADER.C exactly
# =============================================================================
CAN_ID_ENTER_BOOTLOADER = 0x7F0
CAN_ID_START_UPDATE     = 0x7F1
CAN_ID_DATA             = 0x7F2
CAN_ID_PAGE_ACK         = 0x7F3
CAN_ID_END_UPDATE       = 0x7F4
CAN_ID_STATUS           = 0x7F5
CAN_ID_HEARTBEAT        = 0x7F6
CAN_ID_HMAC_CHUNK       = 0x7F7
CAN_ID_QUERY_VERSION    = 0x7F8
CAN_ID_VERSION_RESPONSE = 0x7F9
CAN_ID_QUERY_FRAM_STATE = 0x190  # Query the FM24CL64B's recovered state (application-side, not the bootloader)
CAN_ID_FRAM_STATE_RESP = 0x191   # Answers CAN_ID_QUERY_FRAM_STATE, also sent after an erase
CAN_ID_ERASE_FRAM = 0x192        # Magic-payload erase - see ERASE_FRAM_MAGIC below
CAN_ID_SET_EXPANSION_TYPE = 0x1A0  # Sets/persists which CONN_EXPANSION variant is installed
CAN_ID_EXPANSION_TYPE_RESP = 0x1A1  # Query, or the response to setting it - see EXPANSION.TXT
CAN_ID_SET_FREE_TOOL = 0x1A2  # Sets/persists free_tool_selection - see EEPROM.TXT section 5
CAN_ID_FREE_TOOL_CONFIG_RESP = 0x1A3  # Query, or the response to setting it - carries raw ID-jumper reading too
CAN_ID_SET_DEVICE_SERIAL = 0x1A4  # Sets/persists device_serial_number - see EEPROM.TXT section 6
CAN_ID_PERIPHERAL_INFO_RESP = 0x1A5  # Query, or the response to setting it - carries peripheral type too
CAN_ID_SET_FREE_TOOL = 0x1A2  # Sets/persists free_tool_selection - see EEPROM.TXT section 5
CAN_ID_FREE_TOOL_RESP = 0x1A3  # Query, or the response to setting it

# Index in this list matches the value URTC actually stores (0-4) - see
# EXPANSION.TXT for what each configuration is.
EXPANSION_BOARD_TYPES = [
    "0 - None installed",
    "1 - Basic, TMC2209",
    "2 - Basic, TMC5160A",
    "3 - Advanced, TMC2209 + STM32F051T8",
    "4 - Advanced, TMC5160A + STM32F051T8",
]
ERASE_FRAM_MAGIC = bytes([0xE3, 0xA5, 0xE0, 0xFF])
CAN_ID_BOOTLOADER_VERSION_RESPONSE = 0x7FA  # sent only by the bootloader, alongside 0x7F9, when it's the one answering

STATUS_NAMES = {
    0x01: "Listening",
    0x02: "Erasing backup slot",
    0x03: "Receiving firmware data",
    0x06: "Verifying (size/CRC32/HMAC/HardwareID)",
    0x07: "Copying backup into main slot",
    0x04: "Verify OK - jumping to new firmware",
    0x05: "Verify FAILED - main slot untouched",
    0xFF: "Error",
}

# Matches BOOTLOADER.C's VERIFY_FAIL_REASON_* defines exactly - present as
# a second byte on STATUS_VERIFY_FAIL (0x05) frames specifically, DLC=2
# instead of the usual DLC=1, so a failed update can say WHY rather than
# just THAT. Byte[0] is still 0x05 regardless, so any code that
# only reads that first byte keeps working unchanged.
VERIFY_FAIL_REASONS = {
    0x01: "incomplete transfer (didn't receive the declared size or all 4 HMAC chunks)",
    0x02: "CRC32 mismatch (transfer corruption)",
    0x03: "HMAC signature mismatch (not signed with this project's key)",
    0x04: "HardwareID mismatch (image built for different hardware)",
    0x05: "rollback rejected (older version than what's already installed)",
}

# THIS_HARDWARE_ID and HMAC_KEY must match BOOTLOADER.C exactly, or every
# update this tool sends will be rejected (HardwareID mismatch) or fail
# signature verification (HMAC mismatch). If you change the key in
# BOOTLOADER.C, change it here too - the two are not automatically kept in
# sync.
THIS_HARDWARE_ID = 0x0303CC01  # STM32F303CCT6, URTC board revision 1
FIRMWARE_VERSION_MAJOR = 1
FIRMWARE_VERSION_MINOR = 0

# This tool's OWN version (shown in the banner and window title) -
# deliberately separate from FIRMWARE_VERSION_MAJOR/MINOR above, which is
# the *board firmware's* version this tool writes into the end-update
# frame. The two will often move together but aren't the same number by
# definition - this one's about the flasher script itself.
FLASHER_VERSION = "1.1"
FLASHER_AUTHOR = "JuanenRac"
HMAC_KEY = bytes([
    0x55, 0x52, 0x54, 0x43, 0x2D, 0x48, 0x59, 0x44,
    0x52, 0x41, 0x2D, 0x55, 0x4D, 0x43, 0x2D, 0x32,
    0x30, 0x32, 0x36, 0x2D, 0x43, 0x48, 0x41, 0x4E,
    0x47, 0x45, 0x2D, 0x4D, 0x45, 0x2D, 0x21, 0x21
])

APP_MAX_SIZE = 112 * 1024
FLASH_PAGE_SIZE = 2048
BITRATE_500K_SLCAN_CODE = "6"  # SLCAN's "Sx" bitrate codes: 6 = 500 kbit/s
# Full standard SLCAN/Lawicel bitrate code table, for the bitrate selector
# and auto-detect (see FlasherGUI.auto_detect_bitrate). 500k stays the
# default everywhere else in this file - URTC's own bus is fixed at 500k -
# but a mis-set adapter or a non-standard board might need a different one.
SLCAN_BITRATES = [
    ("10 kbit/s", "0"), ("20 kbit/s", "1"), ("50 kbit/s", "2"),
    ("100 kbit/s", "3"), ("125 kbit/s", "4"), ("250 kbit/s", "5"),
    ("500 kbit/s", "6"), ("800 kbit/s", "7"), ("1 Mbit/s", "8"),
]

# Full-chip SWD/JTAG programming (see SWDFlasher below) - same two fixed
# addresses used throughout BOOTLOADER.C (MAIN_APP_ADDR) and the README's
# documented JTAG bring-up procedure.
BOOTLOADER_FLASH_ADDR = 0x08000000
APP_FLASH_ADDR = 0x08008000
BOOTLOADER_MAX_SIZE = 32 * 1024  # matches BOOTLOADER.C's own 32KB region, 0x08000000-0x08008000
# Verify this against `pyocd list --targets --name stm32f303` on the
# machine actually running this - STM32 coverage in pyOCD is broad, but
# this exact string wasn't confirmed against a live pyOCD install while
# writing this. If it's not found, `pyocd pack install stm32f303cc`
# (or the closest match `pyocd pack find` shows) pulls the CMSIS-Pack.
PYOCD_TARGET_NAME = "stm32f303cc"

# firmware/ lives INSIDE tools/, not as a sibling - this keeps the whole
# tools/ folder self-contained. Someone who just wants to flash a board
# doesn't need the rest of the repo (schematics, 3D files, docs) - they can
# copy tools/ on its own (a USB stick, a shared network folder, wherever)
# and it still works, since the firmware it needs travels with it.
#
# sys.frozen / sys.executable handling: PyInstaller's --onefile mode
# extracts the bundled app into a temporary folder at runtime, and __file__
# inside that running code points into THAT temp folder, not to wherever
# the actual .exe sits on disk - so building the path from __file__ finds a
# firmware/ folder that doesn't exist (or worse, a stale one from a
# previous run, since PyInstaller doesn't always clean these up
# immediately). sys.executable, by contrast, is the real path to the
# running .exe itself, which is what "next to the exe" actually means.
if getattr(sys, "frozen", False):
    base_dir = os.path.dirname(sys.executable)
else:
    base_dir = os.path.dirname(os.path.abspath(__file__))
FIRMWARE_FOLDER = os.path.normpath(os.path.join(base_dir, "firmware"))
LOGS_FOLDER = os.path.normpath(os.path.join(base_dir, "logs"))
CONFIG_FILE_PATH = os.path.normpath(os.path.join(base_dir, "urtc_config.json"))
LANGUAGE_FOLDER = os.path.normpath(os.path.join(base_dir, "language"))
DEFAULT_LANGUAGE = "english"
AVAILABLE_LANGUAGES = [
    ("english", "English"),
    ("spanish", "Español"),
    ("italian", "Italiano"),
    ("french", "Français"),
    ("german", "Deutsch"),
]


def load_config():
    """Reads the same urtc_config.json that _load_config_overrides below
    already reads for its own technical keys (hardware_id, hmac_key_hex,
    etc.) - this just reads the whole file as a plain dict, so the
    language preference can live in the same file without needing a
    second config file per tool. Missing/broken file both fall back to
    an empty dict, same reasoning as _load_config_overrides' own
    fallback just below."""
    if not os.path.isfile(CONFIG_FILE_PATH):
        return {}
    try:
        with open(CONFIG_FILE_PATH, encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return {}


def save_config(updates):
    """Merges updates into the existing urtc_config.json and writes the
    result back - critically, this preserves whatever technical override
    keys (hardware_id, hmac_key_hex, memory-map values) a user may have
    already hand-edited into this file, rather than clobbering them when
    saving just the language preference. Written atomically (temp file +
    os.replace()) - a plain open(..., "w") would leave the real file
    truncated or half-written if the process was interrupted mid-write;
    os.replace() only ever swaps in a fully-written file."""
    current = load_config()
    current.update(updates)
    tmp_path = CONFIG_FILE_PATH + ".tmp"
    try:
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(current, f, indent=2)
        os.replace(tmp_path, CONFIG_FILE_PATH)
        return True
    except OSError:
        try:
            os.remove(tmp_path)
        except OSError:
            pass
        return False


def load_language(name):
    """Reads language/<name>.lng - a plain UTF-8 KEY=Value text file, one
    entry per line, '#'-prefixed lines and blank lines ignored, and a
    literal two-character \\n inside a value unescaped into a real
    newline. Falls back to an empty dict if the file is missing or
    unreadable, so _()'s own fallback-to-key-itself behavior takes over
    rather than crashing the tool over a missing/corrupt translation file."""
    path = os.path.join(LANGUAGE_FOLDER, f"{name}.lng")
    result = {}
    try:
        with open(path, encoding="utf-8-sig") as f:
            for line in f:
                line = line.rstrip("\n").rstrip("\r")
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _sep, value = line.partition("=")
                result[key.strip()] = value.replace("\\n", "\n")
    except (OSError, UnicodeDecodeError):
        return {}
    return result


_config = load_config()
_current_language = _config.get("language", DEFAULT_LANGUAGE)
_translations = load_language(_current_language)
if not _translations and _current_language != DEFAULT_LANGUAGE:
    _current_language = DEFAULT_LANGUAGE
    _translations = load_language(DEFAULT_LANGUAGE)


def _(key, **kwargs):
    """Looks up key in the currently loaded language file and applies any
    keyword substitutions. Falls back to the key itself, visibly
    un-translated, if missing - easier to notice and report than a
    silent KeyError."""
    text = _translations.get(key, key)
    if kwargs:
        try:
            return text.format(**kwargs)
        except (KeyError, IndexError):
            return text
    return text


def _load_config_overrides(log=None):
    """Optional urtc_config.json next to firmware/ can override HMAC_KEY,
    THIS_HARDWARE_ID, and the memory-map constants (slot sizes, flash page
    size, base addresses) without needing to edit or rebuild this script -
    useful for a different board revision, a rotated signing key, or (for
    the memory-map values) adapting this tool to a different chip variant
    or partition scheme down the line. Missing file is normal and silent
    (falls back to the compiled-in defaults above); a present-but-invalid
    file is logged and then also falls back to defaults, rather than
    crashing the whole tool over a typo in a config file.

    Expected format (every key optional - only override what's actually
    changing):
        {
          "hardware_id": "0x0303CC01",
          "hmac_key_hex": "555254432D...",
          "app_max_size": 114688,
          "bootloader_max_size": 32768,
          "flash_page_size": 2048,
          "bootloader_flash_addr": "0x08000000",
          "app_flash_addr": "0x08008000"
        }
    """
    log = log or (lambda msg: None)
    hw_id, key = THIS_HARDWARE_ID, HMAC_KEY
    app_max_size, bootloader_max_size = APP_MAX_SIZE, BOOTLOADER_MAX_SIZE
    flash_page_size = FLASH_PAGE_SIZE
    bootloader_addr, app_addr = BOOTLOADER_FLASH_ADDR, APP_FLASH_ADDR
    defaults = (hw_id, key, app_max_size, bootloader_max_size, flash_page_size, bootloader_addr, app_addr, False)
    if not os.path.isfile(CONFIG_FILE_PATH):
        return defaults

    try:
        with open(CONFIG_FILE_PATH, "r", encoding="utf-8") as f:
            cfg = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        # Nothing to salvage here - the file couldn't even be read as JSON
        # at all, so there's no per-field data to fall back on individually.
        log(f"WARNING: couldn't load {CONFIG_FILE_PATH} ({e}) - using compiled-in defaults instead.")
        return defaults

    # Each field is parsed independently below - a mistake in one (a typo
    # in a rarely-touched memory-map value, say) only reverts THAT field to
    # its compiled-in default and logs which one, rather than discarding
    # every other field that parsed correctly, including a HMAC key or
    # HardwareID the user got right.
    overridden = []
    skipped = []

    def _int_field(name, current):
        # Same string-or-number tolerance as hardware_id below - a
        # hand-edited JSON config is just as likely to have either.
        if name not in cfg:
            return current
        try:
            val = cfg[name]
            if isinstance(val, bool):
                # Python's bool is a subclass of int, so isinstance(val, str)
                # being False for a bool falls through to int(val) below,
                # which happily returns 1/0 for True/False with no error at
                # all - checked explicitly, ahead of that, so a JSON "true"
                # gets reported as skipped rather than silently becoming 1.
                raise TypeError(f"{name} must be a number or numeric string, not a boolean")
            if isinstance(val, float) and not val.is_integer():
                # int(114688.5) truncates to 114688 with no error at all -
                # same silent-precision-loss shape as the bool case above.
                # A float that happens to represent an exact integer
                # (114688.0) is fine to accept below; one with a real
                # fractional part almost certainly means a config mistake
                # for a memory address/size field, worth reporting rather
                # than quietly rounding down.
                raise TypeError(f"{name} has a fractional value ({val}), not a whole number")
            result = int(val, 0) if isinstance(val, str) else int(val)
            if not (0 <= result <= 0xFFFFFFFF):
                raise ValueError(f"{result} is out of range for a 32-bit value")
            overridden.append(name)
            return result
        except (ValueError, TypeError) as e:
            skipped.append(f"{name} ({e})")
            return current

    if "hardware_id" in cfg:
        try:
            val = cfg["hardware_id"]
            if isinstance(val, bool):
                raise TypeError("hardware_id must be a number or numeric string, not a boolean")
            # Accepts either a JSON string ("0x0303CC01" or a plain decimal
            # string) or a plain JSON number (50580689) - both are natural
            # ways to write this in a hand-edited config file, and int()'s
            # own two-argument form only accepts a string for the first
            # argument, raising TypeError on a bare JSON number otherwise.
            hw_id = int(val, 0) if isinstance(val, str) else int(val)
            if not (0 <= hw_id <= 0xFFFFFFFF):
                raise ValueError(f"{hw_id} is out of range for a 32-bit HardwareID")
            overridden.append("hardware_id")
        except (ValueError, TypeError) as e:
            skipped.append(f"hardware_id ({e})")
    if "hmac_key_hex" in cfg:
        try:
            candidate_key = bytes.fromhex(cfg["hmac_key_hex"])
            if len(candidate_key) != 32:
                raise ValueError(f"must decode to 32 bytes, got {len(candidate_key)}")
            key = candidate_key
            overridden.append("hmac_key_hex")
        except (ValueError, TypeError) as e:
            skipped.append(f"hmac_key_hex ({e})")
    app_max_size = _int_field("app_max_size", app_max_size)
    bootloader_max_size = _int_field("bootloader_max_size", bootloader_max_size)
    flash_page_size = _int_field("flash_page_size", flash_page_size)
    bootloader_addr = _int_field("bootloader_flash_addr", bootloader_addr)
    app_addr = _int_field("app_flash_addr", app_addr)

    if overridden:
        log(f"Loaded config overrides from {CONFIG_FILE_PATH}: {', '.join(overridden)}")
    if skipped:
        log(f"WARNING: {CONFIG_FILE_PATH} had problems with: {'; '.join(skipped)} "
            f"- those specific fields fell back to their compiled-in defaults, "
            f"everything else above still applied.")
    return (hw_id, key, app_max_size, bootloader_max_size, flash_page_size,
            bootloader_addr, app_addr, bool(overridden))


# Applied at import time with no logger (silent) so every other module-level
# constant below sees the right values immediately; FlasherGUI.__init__ and
# run_cli() both re-run this WITH logging once a log sink actually exists,
# purely so the override (or its absence) is visible in the session log/CLI
# output - the values themselves don't change on the second call.
THIS_HARDWARE_ID, HMAC_KEY, APP_MAX_SIZE, BOOTLOADER_MAX_SIZE, FLASH_PAGE_SIZE, \
    BOOTLOADER_FLASH_ADDR, APP_FLASH_ADDR, _CONFIG_LOADED = _load_config_overrides()

# The banner image is a bundled asset, not a user-supplied file - a
# genuinely different location than FIRMWARE_FOLDER above when frozen.
# PyInstaller's --onefile mode extracts files added via --add-data to a
# temporary directory at sys._MEIPASS, separate from wherever the .exe
# itself sits (that's base_dir, used above for firmware/ specifically
# because it's meant to be user-editable without a rebuild).
if getattr(sys, "frozen", False):
    _assets_base = getattr(sys, "_MEIPASS", base_dir)
else:
    _assets_base = os.path.dirname(os.path.abspath(__file__))
BANNER_IMAGE_PATH = os.path.normpath(os.path.join(_assets_base, "assets", "urtc_banner.png"))
ICON_IMAGE_PATH = os.path.normpath(os.path.join(_assets_base, "assets", "urtc_icon.png"))


def _center_geometry(win, width, height):
    """Returns a "WxH+X+Y" geometry string centered on the screen - shared
    by the splash, main window, and any popup (About/License) that wants
    to be centered rather than placed wherever the window manager's
    default happens to be. Uses the caller's own known target width/
    height rather than winfo_width()/height(), which depend on the
    window having already been fully realized/mapped by the window
    manager - a real source of "it isn't actually centered" bugs if
    queried too early, since those can still report a stale placeholder
    size at that point. On a multi-monitor system, winfo_screenwidth()/
    height() report the full virtual desktop Tkinter sees, not
    necessarily one physical monitor - which is a Tkinter/Tk limitation
    this can't fully work around without a platform-specific dependency,
    but it's still correctly centered on that combined canvas, matching
    what plain Tkinter apps generally do.
    """
    win.update_idletasks()
    ws = win.winfo_screenwidth()
    hs = win.winfo_screenheight()
    x = max(0, (ws - width) // 2)
    y = max(0, (hs - height) // 2)
    return f"{width}x{height}+{x}+{y}"

# Same 12 tool profiles as tester_config.py's own TOOL_NAMES - kept as a
# separate copy rather than a shared import between the two tools' own
# config modules (they're intentionally independent projects, not a
# shared package), used here only for the free tool configuration
# picker (0x1A2) in the CAN OTA tab.
TOOL_NAMES = {
    0: "Soldering Iron", 1: "Paste Dispenser", 2: "Liquid Dispenser",
    3: "Screwdriver", 4: "Vacuum Pickup", 5: "Drill",
    6: "Gripper (Gimbal)", 7: "Gripper (NEMA)", 8: "AOI Inspection",
    9: "Laser Engraver", 10: "3D Printer", 11: "Scan Probe",
}
