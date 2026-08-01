# =============================================================================
# URTC Tester - Configuration, language/translation loading, and shared
# constants used by every other module in this project
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import sys
import os
import json
import re

try:
    import serial
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False


TESTER_VERSION = "1.1"
TESTER_AUTHOR = "JuanenRac"

# THIS_HARDWARE_ID matches BOOTLOADER.C/STM32F303CC.C's own constant - used
# here only to flag a mismatch after connecting (a different board/rig
# answering than the one this was set up for), never to gate anything.
THIS_HARDWARE_ID = 0x0303CC01

BITRATE_500K_SLCAN_CODE = "6"  # SLCAN's "Sx" bitrate codes: 6 = 500 kbit/s
SLCAN_BITRATES = [
    ("10 kbit/s", "0"), ("20 kbit/s", "1"), ("50 kbit/s", "2"),
    ("100 kbit/s", "3"), ("125 kbit/s", "4"), ("250 kbit/s", "5"),
    ("500 kbit/s", "6"), ("800 kbit/s", "7"), ("1 Mbit/s", "8"),
]

if getattr(sys, "frozen", False):
    base_dir = os.path.dirname(sys.executable)
else:
    base_dir = os.path.dirname(os.path.abspath(__file__))
LOGS_FOLDER = os.path.normpath(os.path.join(base_dir, "logs"))
CUSTOM_IDS_PATH = os.path.normpath(os.path.join(base_dir, "urtc_custom_ids.json"))
LANGUAGE_FOLDER = os.path.normpath(os.path.join(base_dir, "language"))
CONFIG_PATH = os.path.normpath(os.path.join(base_dir, "config.json"))
DEFAULT_LANGUAGE = "english"
AVAILABLE_LANGUAGES = [
    ("english", "English"),
    ("spanish", "Español"),
    ("italian", "Italiano"),
    ("french", "Français"),
    ("german", "Deutsch"),
]


def load_config():
    """Reads config.json next to this tool - currently just the language
    preference, but a plain dict so future settings can be added here
    without needing a new file or a schema migration. Missing file (first
    run) or a broken one (manual edit gone wrong) both fall back to an
    empty dict rather than crashing the tool - every read of a key from
    this uses .get() with a sensible default anyway."""
    if not os.path.isfile(CONFIG_PATH):
        return {}
    try:
        with open(CONFIG_PATH, encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return {}


def save_config(updates):
    """Merges updates into whatever's already in config.json and writes
    the result back - merges rather than overwrites wholesale, so saving
    one setting (e.g. language) never clobbers some other setting a
    future version of this tool might have already written there.
    Failure to write (read-only filesystem, permissions) is logged by the
    caller if it wants to, not raised here - a missing saved preference
    is a minor inconvenience, not worth crashing over. Written atomically
    (temp file + os.replace()) - a plain open(..., "w") would leave the
    real file truncated or half-written if the process was interrupted
    mid-write."""
    current = load_config()
    current.update(updates)
    tmp_path = CONFIG_PATH + ".tmp"
    try:
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(current, f, indent=2)
        os.replace(tmp_path, CONFIG_PATH)
        return True
    except OSError:
        try:
            os.remove(tmp_path)
        except OSError:
            pass
        return False


def load_language(name):
    """Reads language/<name>.lng - a plain UTF-8 KEY=Value text file, one
    entry per line, '#' - prefixed lines and blank lines ignored, and a
    literal two-character \\n inside a value unescaped into a real
    newline (for the handful of multi-line messagebox strings). Falls
    back to an empty dict if the file is missing or unreadable, so the
    caller's own fallback-to-key-itself behavior in _() takes over rather
    than crashing the tool over a missing/corrupt translation file."""
    path = os.path.join(LANGUAGE_FOLDER, f"{name}.lng")
    # name comes from urtc_config.json - not sanitized before this point,
    # so a manipulated config file could otherwise smuggle in a path
    # separator or "../" sequence and make this read a file outside
    # LANGUAGE_FOLDER entirely. Restricting to a plain filename-safe
    # character set closes that off regardless of what's actually in the
    # AVAILABLE_LANGUAGES list.
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        return {}
    result = {}
    try:
        with open(path, encoding="utf-8") as f:
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
    # Saved preference pointed at a language file that's now missing/broken
    # (moved, deleted, hand-edited badly) - falls back to English rather
    # than showing raw translation keys throughout the entire UI.
    _current_language = DEFAULT_LANGUAGE
    _translations = load_language(DEFAULT_LANGUAGE)


def _(key, **kwargs):
    """Looks up key in the currently loaded language file and applies any
    keyword substitutions (e.g. _("GREETING", name=x) for a string
    containing {name}). Falls back to the key itself, visibly
    un-translated, if the key is missing from the file entirely - a
    visible wrong string in the UI is far easier to notice and report
    than a silent KeyError crashing the tool."""
    text = _translations.get(key, key)
    if kwargs:
        try:
            return text.format(**kwargs)
        except (KeyError, IndexError):
            return text
    return text


def _load_custom_id_names():
    """Optional, not included by default: urtc_custom_ids.json next to
    this script, mapping hex ID strings to friendly names - e.g.
    {"0x199": "My Expansion Sensor"}. Lets someone testing a custom
    expansion board or addition give their own IDs a readable name in the
    Raw Bus Monitor without needing to modify this tool's source at all.
    Missing file is silent (this is opt-in); a present-but-broken file is
    logged and ignored rather than crashing the tool over a typo."""
    if not os.path.isfile(CUSTOM_IDS_PATH):
        return {}
    try:
        with open(CUSTOM_IDS_PATH, encoding="utf-8") as f:
            raw = json.load(f)
        result = {}
        for key, name in raw.items():
            try:
                result[int(key, 0)] = str(name)
            except (ValueError, TypeError):
                continue  # one bad entry doesn't invalidate the rest of the file
        return result
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return {}


CUSTOM_ID_NAMES = _load_custom_id_names()

if getattr(sys, "frozen", False):
    BANNER_IMAGE_PATH = os.path.normpath(os.path.join(sys._MEIPASS, "assets", "urtc_tester_banner.png"))
    ICON_IMAGE_PATH = os.path.normpath(os.path.join(sys._MEIPASS, "assets", "urtc_icon.png"))
else:
    BANNER_IMAGE_PATH = os.path.normpath(os.path.join(base_dir, "assets", "urtc_tester_banner.png"))
    ICON_IMAGE_PATH = os.path.normpath(os.path.join(base_dir, "assets", "urtc_icon.png"))


def list_serial_ports():
    ports = []
    if HAVE_SERIAL:
        import serial.tools.list_ports
        ports = [p.device for p in serial.tools.list_ports.comports()]
    return ports

# CAN protocol constants - every ID this tool can send or receive, matching
# CANBUS.TXT exactly (kept as one flat block here rather than split per
# tool, since several - like 0x100 and 0x110/0x111 - are global and used
# regardless of which tool profile is active).
# =============================================================================
CAN_ID_IMPACT_EVENT      = 0x095  # Scan probe - max priority, event-driven
CAN_ID_GLOBAL_STATUS     = 0x100  # Status LED + ring LED + OLED night mode - any tool
CAN_ID_QUERY_ACTIVE_TOOL = 0x110  # Query which tool profile is active + quick state
CAN_ID_ACTIVE_TOOL_RESP  = 0x111  # Answers CAN_ID_QUERY_ACTIVE_TOOL
CAN_ID_MOTION_CMD        = 0x120  # Stepper dir+steps - dispensers/screwdriver/grippers
CAN_ID_SOLDER_SETPOINT   = 0x130  # T12 setpoint temperature
CAN_ID_SOLDER_TELEMETRY  = 0x135  # T12 actual temperature + endstop
CAN_ID_DRILL_CMD         = 0x140  # BL4260 speed + direction
CAN_ID_VACUUM_TELEMETRY  = 0x145  # ADC reading + digital detect
CAN_ID_DRILL_TELEMETRY   = 0x147  # Actual RPM + endstop
CAN_ID_AOI_CMD           = 0x150  # Ring mode + strobe period
CAN_ID_AOI_TELEMETRY     = 0x155  # Endstop only
CAN_ID_LASER_CMD         = 0x160  # Power + interlock
CAN_ID_LASER_TELEMETRY   = 0x165  # Endstop only
CAN_ID_3DP_THERMAL_MOTION = 0x170  # Nozzle setpoint + extruder direction/steps
CAN_ID_3DP_LAYER_FAN_CMD  = 0x173  # Layer fan PWM
CAN_ID_3DP_HOTEND_TELEM   = 0x175  # Hotend actual temperature
CAN_ID_3DP_LAYER_FAN_RPM  = 0x177  # Layer fan actual RPM
CAN_ID_3DP_HOTEND_FAN_CMD = 0x178  # Hotend fan PWM
CAN_ID_3DP_HOTEND_FAN_RPM = 0x179  # Hotend fan actual RPM
CAN_ID_EXP_SPI_CMD        = 0x180  # Generic SPI passthrough request, for CONN_EXPANSION
CAN_ID_EXP_SPI_RESP       = 0x181  # Answers CAN_ID_EXP_SPI_CMD
CAN_ID_QUERY_DIAG0        = 0x182  # Query TMC_DIAG0's current level
CAN_ID_DIAG0_RESP         = 0x183  # Answers CAN_ID_QUERY_DIAG0
CAN_ID_QUERY_FRAM_STATE = 0x190  # Query the FM24CL64B's recovered state
CAN_ID_FRAM_STATE_RESP  = 0x191  # Answers CAN_ID_QUERY_FRAM_STATE, also sent after an erase
CAN_ID_ERASE_FRAM       = 0x192  # Magic-payload erase - see ERASE_FRAM_MAGIC below
CAN_ID_EXPANSION_TYPE_RESP = 0x1A1  # Query (empty payload), or the response - see EXPANSION.TXT
CAN_ID_FREE_TOOL_CONFIG_RESP = 0x1A3  # Query (empty payload), or the response - see EEPROM.TXT section 5
CAN_ID_PERIPHERAL_INFO_RESP = 0x1A5  # Query (empty payload), or the response - see EEPROM.TXT section 6

# Same 5 configurations as urtc_flasher.py's own list - kept in sync manually
# since these two tools don't share a module.
EXPANSION_BOARD_TYPES = [
    "0 - None installed",
    "1 - Basic, TMC2209",
    "2 - Basic, TMC5160A",
    "3 - Advanced, TMC2209 + STM32F051T8",
    "4 - Advanced, TMC5160A + STM32F051T8",
]
ERASE_FRAM_MAGIC = bytes([0xE3, 0xA5, 0xE0, 0xFF])
CAN_ID_QUERY_VERSION     = 0x7F8  # Answered by app or bootloader, whichever's running
CAN_ID_VERSION_RESPONSE  = 0x7F9

# Layers built-in names (derived from every CAN_ID_* constant just above,
# via globals() - not a separately hand-maintained list that could drift)
# underneath whatever urtc_custom_ids.json already loaded into
# CUSTOM_ID_NAMES above. setdefault leaves any custom override already
# present alone - so a custom file that deliberately renames a standard ID
# still wins, and everything else gets a sensible built-in name instead of
# nothing at all.
for _name, _value in list(globals().items()):
    if _name.startswith("CAN_ID_") and isinstance(_value, int):
        CUSTOM_ID_NAMES.setdefault(_value, _name[len("CAN_ID_"):].replace("_", " ").title())
del _name, _value

TOOL_NAMES = {
    0: "Soldering Iron", 1: "Paste Dispenser", 2: "Liquid Dispenser",
    3: "Screwdriver", 4: "Vacuum Pickup", 5: "Drill",
    6: "Gripper (Gimbal)", 7: "Gripper (NEMA)", 8: "AOI Inspection",
    9: "Laser Engraver", 10: "3D Printer", 11: "Scan Probe",
}
# 5 tool IDs all share the exact same motion command (0x120) - a plain
# stepper with direction + step count, differing only in what's physically
# attached, not in the protocol.
MOTION_TOOL_IDS = {1, 2, 3, 6, 7}



def _center_geometry(win, width, height):
    """Returns a "WxH+X+Y" geometry string centered on the screen - shared
    by the splash, main window, and any popup (About/License) that wants
    to be centered rather than placed wherever the window manager's
    default happens to be. Uses the caller's own known target width/
    height rather than winfo_width()/height(), which depend on the
    window having already been fully realized/mapped by the window
    manager - a real source of "it isn't actually centered" bugs if
    queried too early, since those can still report a stale placeholder
    size at that point."""
    win.update_idletasks()
    ws = win.winfo_screenwidth()
    hs = win.winfo_screenheight()
    x = max(0, (ws - width) // 2)
    y = max(0, (hs - height) // 2)
    return f"{width}x{height}+{x}+{y}"
