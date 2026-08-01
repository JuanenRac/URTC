# =============================================================================
# URTC Flasher - SWD/JTAG full-chip programming via STM32CubeProgrammer or
# pyOCD, run as external subprocesses (a DIFFERENT kind of operation from
# the CAN bootloader protocol in flasher_protocol.py)
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import os
import platform
import re
import shutil
import subprocess
import threading
import time

from flasher_config import _, APP_FLASH_ADDR, BOOTLOADER_FLASH_ADDR, PYOCD_TARGET_NAME

# =============================================================================
# SWD/JTAG full-chip programming - a DIFFERENT kind of operation from the CAN
# OTA path above, deliberately kept as its own separate classes rather than
# folded into URTCFlasher. The CAN update path is self-healing on any
# interruption - the bootloader's own golden-image logic (see BOOTLOADER.C)
# guarantees the board always has SOME working firmware afterward, with no
# physical access needed to recover. A full-chip SWD write isn't
# self-healing the same way: an interrupted erase/write can leave the
# board with no valid firmware running until it's reprogrammed. That's a
# real difference worth keeping separate in the UI - but it's not a
# "brick" in the permanent sense: the SWD/debug port itself doesn't depend
# on flash contents, so reconnecting and flashing again from scratch
# recovers it, the same way a first-ever bring-up does. Nothing in either
# class below touches option bytes, so there's no path here to the one
# STM32 failure mode that actually IS irreversible (RDP level 2 read
# protection permanently disabling the debug port) - that would need a
# deliberate separate action this code never performs.
#
# DESIGN NOTE - CLI over Python API: pyOCD does expose a Python API
# (FileProgrammer, ConnectHelper, etc.) that would integrate more natively
# with this GUI's progress bar and log widget than shelling out to a
# subprocess. It was deliberately NOT used here. pyOCD's own README
# describes its Python API as "beta quality" that "will be changed" before
# a 1.0 release, while the `pyocd` command-line tool's flags (`flash
# --base-address`, `erase --chip`, etc.) are the documented, stable
# surface. Since none of this can be tested against real hardware in the
# environment that wrote it, the more stable and externally-verifiable
# interface is the safer choice, even at the cost of parsing subprocess
# output instead of getting live Python callbacks. If you'd rather have
# the tighter API integration and are willing to pin an exact pyOCD
# version and verify the calls yourself, that's a reasonable thing to
# switch to later - just not a safe default to guess at here.
#
# BOTH classes below share the same shape (find the executable, run it,
# stream output to the log, raise on nonzero exit) so the GUI can treat
# them interchangeably, the same way SLCAN/SocketCAN share an interface
# for the CAN path.
# =============================================================================
class SWDFlashError(Exception):
    pass


def _find_executable(names):
    """Try each name/path in order via shutil.which (which also handles
    absolute paths that already exist), return the first hit or None."""
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def _address_args_bin_needs_it(path, address, flag_before=None):
    """.hex, .elf/.axf, and Motorola S-Record (.srec/.s19/.s37) all embed
    their own load address and should be passed as-is; anything else is
    treated as a raw binary needing an address passed explicitly - not
    just files literally named ".bin", since the file pickers' "All
    files" option lets someone select a raw image under any extension
    (.img, .rom, none at all), and a name alone doesn't change what
    format the bytes are actually in. Returns the extra CLI tokens to
    append."""
    if not path.lower().endswith((".hex", ".elf", ".axf", ".srec", ".s19", ".s37")):
        addr_str = f"0x{address:08X}"  # zero-padded to the conventional 8 hex digits for a 32-bit ARM address - hex() alone would drop leading zeros (0x8000000, 7 digits) which some strict CLI parsers could misread
        return ([flag_before, addr_str] if flag_before else [addr_str])
    return []


class _SubprocessProgrammerBase:
    """Shared run/log/error-handling plumbing for both CLI wrappers below."""
    NAME = "programmer"

    # Substrings that mean "this definitely failed" wherever they appear in
    # stdout/stderr, checked case-insensitively. This exists because a real
    # test against STM32CubeProgrammer with no probe connected returned exit
    # code 0 and still logged "complete" - the tool's own exit code turned
    # out not to be a reliable success/failure signal by itself, so output
    # content is checked too. This list is necessarily a floor, not a
    # ceiling (it can't cover every possible failure message from a tool
    # this code doesn't control) - see check_connection() on each subclass
    # below for the other half of the defense: requiring POSITIVE evidence
    # of a real connection before ever proceeding, rather than only
    # screening for known-bad text.
    _FAILURE_INDICATORS = [
        "no st-link", "no st link", "no debug probe", "unable to connect",
        "not detected", "no target", "no target connected", "failed to connect",
        "connection failed", "no probes", "cannot connect", "could not connect",
        "communication error", "error:", "no device found", "target is not responding",
        "mismatch", "does not match", "differ at", "verification failed",
    ]

    def __init__(self, log=None):
        self.log = log or (lambda msg: None)
        self.exe = None  # set by subclass __init__

    def _looks_like_failure(self, text, exclude_args=()):
        # Strip anything that looks like a real path before scanning - a
        # tool echoing back the exact file path it was given (a common,
        # ordinary progress message, e.g. "Loading C:\test_error_logs\app.bin")
        # shouldn't be able to trigger a false failure just because the
        # user's own folder or file name happens to contain a word like
        # "error" or "mismatch". Catches both absolute/relative paths with
        # a separator AND a bare relative filename with none (e.g.
        # "app_error.bin" sitting in the current directory) by also
        # checking for a known firmware file extension.
        lower = text.lower()
        path_exts = (".bin", ".hex", ".elf", ".axf")
        for arg in exclude_args:
            arg_lower = arg.lower()
            looks_like_path = "/" in arg or "\\" in arg or arg_lower.endswith(path_exts)
            if looks_like_path and arg_lower in lower:
                lower = lower.replace(arg_lower, "")
        return any(indicator in lower for indicator in self._FAILURE_INDICATORS)

    def _run(self, args, dry_run, timeout=300, check_output=True):
        if not self.exe:
            raise SWDFlashError(f"{self.NAME} executable not found on this system")
        cmd = [self.exe] + args
        self.log("$ " + " ".join(cmd))
        if dry_run:
            self.log(_("LOG_DRY_RUN_NOT_EXECUTED"))
            return ""
        try:
            proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1,
            )
        except OSError as e:
            raise SWDFlashError(f"couldn't run {self.NAME}: {e}")

        timed_out = threading.Event()
        timer = threading.Timer(timeout, lambda: (timed_out.set(), proc.kill()))
        timer.start()
        lines = []
        try:
            for line in proc.stdout:
                line = line.rstrip("\n")
                lines.append(line)
                self.log("  " + line)
            proc.wait()
        finally:
            timer.cancel()
        combined = "\n".join(lines)
        if timed_out.is_set():
            raise SWDFlashError(f"{self.NAME} timed out after {timeout}s")
        if proc.returncode != 0:
            raise SWDFlashError(
                f"{self.NAME} exited with code {proc.returncode} - see the log above for details"
            )
        if check_output and self._looks_like_failure(combined, exclude_args=args):
            raise SWDFlashError(
                f"{self.NAME} exited with code 0, but its own output contains a "
                f"failure indicator - treating this as a failure rather than "
                f"trusting the exit code alone. See the log above for the exact text."
            )
        return combined


class PyOCDCLI(_SubprocessProgrammerBase):
    NAME = "pyOCD"

    def __init__(self, log=None):
        super().__init__(log)
        self.exe = _find_executable(["pyocd", "pyocd.exe"])

    @staticmethod
    def available():
        return _find_executable(["pyocd", "pyocd.exe"]) is not None

    def list_probes(self):
        """Returns a list of (uid, description) tuples, or [] if none found
        or the list couldn't be parsed. Confirmed real pyOCD output is a
        plain table:
            #   Probe/Board          Unique ID                 Target
            --------------------------------------------------------
            0   STM32 STLink         2900240008000054574E514E   n/a
        so the UID is simply the column after the row number - if a given
        pyOCD version formats this differently, this returns [] rather
        than guessing, and callers should treat that as "couldn't parse,
        ask the user to check `pyocd list --probes` output directly".
        """
        try:
            output = self._run(["list", "--probes"], dry_run=False, check_output=False)
        except SWDFlashError:
            return []
        probes = []
        for line in output.splitlines():
            m = re.match(r"^\s*\d+\s+(\S.*)$", line)
            if not m:
                continue
            rest = m.group(1).strip()
            parts = re.split(r"\s{2,}", rest)  # columns are separated by 2+ spaces in the real table
            if len(parts) < 2:
                continue
            description, uid = parts[0], parts[1]
            probes.append((uid, description))
        return probes

    def check_connection(self, probe_uid=None, dry_run=False):
        # Checks USB-level probe presence via `pyocd list --probes` -
        # necessary but not sufficient (a probe can be plugged into USB but
        # not wired via SWD to the actual target chip), which is why every
        # later step's output is also screened for failure text, not just
        # this one check trusted blindly.
        #
        # Detection is a POSITIVE match for an actual probe list row -
        # confirmed real output looks like a plain table:
        #   #   Probe/Board          Unique ID                 Target
        #   --------------------------------------------------------
        #   0   STM32 STLink         2900240008000054574E514E  n/a
        # so a genuine entry is a line starting with a row number followed
        # by whitespace and more content, which also naturally skips the
        # header (starts with "#") and the separator (starts with "-").
        self.log(_("LOG_CHECKING_PROBE"))
        if dry_run:
            self.log(f"$ {self.exe} list --probes")
            self.log(_("LOG_DRY_RUN_SKIP_CONNECTION"))
            return
        output = self._run(["list", "--probes"], dry_run=False, check_output=False)
        if not re.search(r"^\s*\d+\s+\S", output, re.MULTILINE):
            raise SWDFlashError(
                "No SWD/JTAG probe detected (checked with 'pyocd list --probes' "
                "- expected at least one probe row, found none). Check the "
                "ST-Link/probe is connected via USB before trying again."
            )
        # With more than one probe connected, a command with no --probe
        # targets whichever one the OS happens to enumerate first - not
        # necessarily the board actually meant. Two or more entries with no
        # probe_uid given is refused rather than guessing which one to hit
        # with a chip erase.
        probe_count = len(re.findall(r"^\s*\d+\s+\S", output, re.MULTILINE))
        if probe_count > 1 and not probe_uid:
            raise SWDFlashError(
                f"{probe_count} probes are connected and none was selected - "
                f"refusing to guess which one to run a chip erase against. "
                f"Pick one from the probe list."
            )
        if probe_uid and probe_uid not in output:
            raise SWDFlashError(
                f"Probe UID '{probe_uid}' was specified but doesn't appear in "
                f"'pyocd list --probes' output - it may be mistyped, or that "
                f"probe may no longer be connected. Re-check the UID against "
                f"the current probe list rather than proceeding against "
                f"whichever probe the OS happens to enumerate first."
            )
        self.log(_("LOG_PROBE_DETECTED_USB"))

    def backup_flash(self, output_path, dry_run=False, target=PYOCD_TARGET_NAME, probe_uid=None):
        """Reads the entire 256KB flash region to output_path before any
        erase touches it, via commander's savemem command - see
        CubeProgrammerCLI.backup_flash's own docstring for why this
        matters more than it might for an ordinary read. Returns True if
        the backup file was actually written and is non-empty."""
        probe_args = ["--probe", probe_uid] if probe_uid else []
        self.log(_("LOG_BACKING_UP_FLASH", path=output_path))
        if dry_run:
            self.log(f"$ {self.exe} commander -t {target} " + " ".join(probe_args)
                      + f' -c "savemem 0x08000000 0x40000 \\"{output_path}\\""')
            self.log(_("LOG_DRY_RUN_NOTHING_TO_VERIFY"))
            return False
        self._run(["commander", "-t", target] + probe_args
                  # Quoted per pyOCD's own commander syntax (confirmed in its
                  # docs: words can be single/double-quoted to include
                  # whitespace) - without this, a path containing spaces
                  # gets split into multiple wrong arguments by pyOCD's own
                  # internal command lexer, not just the OS shell. Forward
                  # slashes, not the OS-native separator: pyOCD's commander
                  # parses this string with its own lexer (not the OS
                  # shell), and a backslash there risks being read as an
                  # escape sequence on Windows paths - forward slashes are
                  # accepted for file paths on Windows by essentially every
                  # modern tool, sidestepping the question entirely.
                  + ["-c", f'savemem 0x08000000 0x40000 "{output_path.replace(os.sep, "/")}"'], dry_run=False)
        ok = os.path.isfile(output_path) and os.path.getsize(output_path) > 0
        if ok:
            self.log(_("LOG_BACKUP_WRITTEN", path=output_path, size=os.path.getsize(output_path)))
        else:
            self.log(_("LOG_BACKUP_NO_FILE"))
        return ok

    def full_chip_flash(self, bootloader_path, app_path, dry_run=False,
                         target=PYOCD_TARGET_NAME, probe_uid=None, backup_path=None):
        self.check_connection(probe_uid, dry_run)
        probe_args = ["--probe", probe_uid] if probe_uid else []
        if backup_path is not None:
            if not self.backup_flash(backup_path, dry_run=dry_run, target=target,
                                      probe_uid=probe_uid) and not dry_run:
                raise SWDFlashError(
                    f"Backup to {backup_path} didn't produce a usable file - stopping before "
                    f"the erase rather than proceeding without it. Retry without a backup path "
                    f"if you're confident you don't need one this time."
                )
        self.log(_("LOG_PYOCD_FULLCHIP_HEADER", target=target))
        self.log(_("LOG_ERASING_ENTIRE_CHIP"))
        self._run(["erase", "-t", target, "--chip"] + probe_args, dry_run)

        self.log(_("LOG_PROGRAMMING_BOOTLOADER_REGION"))
        args = ["flash", "-t", target, "-e", "auto"] + probe_args
        args += _address_args_bin_needs_it(bootloader_path, BOOTLOADER_FLASH_ADDR, "--base-address")
        args += [bootloader_path]
        self._run(args, dry_run)

        self.log(_("LOG_PROGRAMMING_APP_REGION"))
        args = ["flash", "-t", target, "-e", "auto"] + probe_args
        args += _address_args_bin_needs_it(app_path, APP_FLASH_ADDR, "--base-address")
        args += [app_path]
        self._run(args, dry_run)

        # pyOCD's own flash command already does an internal CRC-based
        # check before writing (skips pages that already match, per its
        # own "Trust CRC" optimization), but that's not the same as an
        # explicit post-write pass/fail report the way CubeProgrammer's -v
        # flag gives. `commander -c compare` does a real byte-for-byte
        # read-back against the source file, closing that gap - but only
        # for a raw .bin: it compares flash content against the file's raw
        # bytes, which wouldn't correctly match a .hex/.elf file's own
        # text/structured encoding even after a genuinely successful
        # flash, so those formats skip this step and rely on the flash
        # command's own internal verification instead.
        if bootloader_path.lower().endswith(".bin"):
            self.log(_("LOG_VERIFYING_BOOTLOADER_REGION"))
            self._run(["commander", "-t", target] + probe_args
                      + ["-c", f'compare 0x{BOOTLOADER_FLASH_ADDR:08X} "{bootloader_path.replace(os.sep, "/")}"'], dry_run)
        else:
            self.log(_("LOG_BOOTLOADER_NOT_BIN_SKIP"))
        if app_path.lower().endswith(".bin"):
            self.log(_("LOG_VERIFYING_APP_REGION"))
            self._run(["commander", "-t", target] + probe_args
                      + ["-c", f'compare 0x{APP_FLASH_ADDR:08X} "{app_path.replace(os.sep, "/")}"'], dry_run)
        else:
            self.log(_("LOG_APPLICATION_NOT_BIN_SKIP"))

        self.log(_("LOG_RESETTING_TARGET"))
        self._run(["reset", "-t", target] + probe_args, dry_run)
        self.log(_("LOG_PYOCD_FULLCHIP_COMPLETE"))


def _search_windows_registry_for_stm32cubeprogrammer():
    """Looks through the standard Windows uninstall-registry keys for an
    STM32CubeProgrammer install and returns the CLI exe's full path, or
    None if this isn't Windows, winreg isn't available for some reason,
    nothing matching is found, or anything about the search goes wrong.
    Deliberately broad in what it catches (any OSError, not just
    FileNotFoundError) - a registry access failing in some unexpected way
    should fall through to the hardcoded path list below, never be fatal
    to finding the tool at all."""
    if platform.system() != "Windows":
        return None
    try:
        import winreg
    except ImportError:
        return None
    # Both the 64-bit and WOW6432Node (32-bit apps on 64-bit Windows)
    # uninstall keys are checked - the installer's own bitness decides
    # which one it registers under, and that isn't something to assume
    # either way. Both HKLM and HKCU are checked too - a non-admin install
    # writes its uninstaller keys under HKCU instead of HKLM, so an
    # HKLM-only search would miss that install entirely, falling back to
    # the hardcoded paths below (which don't cover every possible install
    # location).
    uninstall_keys = [
        r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
        r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall",
    ]
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for key_path in uninstall_keys:
            try:
                with winreg.OpenKey(hive, key_path) as root_key:
                    for i in range(winreg.QueryInfoKey(root_key)[0]):
                        try:
                            subkey_name = winreg.EnumKey(root_key, i)
                            with winreg.OpenKey(root_key, subkey_name) as subkey:
                                display_name = winreg.QueryValueEx(subkey, "DisplayName")[0]
                                if "stm32cubeprogrammer" not in display_name.lower():
                                    continue
                                install_loc = winreg.QueryValueEx(subkey, "InstallLocation")[0]
                                candidate = os.path.join(install_loc, "bin", "STM32_Programmer_CLI.exe")
                                if os.path.isfile(candidate):
                                    return candidate
                        except OSError:
                            continue  # this specific subkey didn't have what we needed - keep looking, not fatal
            except OSError:
                continue  # this uninstall key doesn't exist on this system - try the other one
    return None


class CubeProgrammerCLI(_SubprocessProgrammerBase):
    NAME = "STM32CubeProgrammer"

    # Fallback search locations if the CLI isn't on PATH - the installer
    # doesn't always add it. Adjust these if your install lives elsewhere;
    # this list is a starting point, not exhaustive across every OS/version.
    _FALLBACK_PATHS = [
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        "/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
        os.path.expanduser("~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"),
    ]

    def __init__(self, log=None):
        super().__init__(log)
        self.exe = _find_executable(["STM32_Programmer_CLI", "STM32_Programmer_CLI.exe"])
        if not self.exe:
            self.exe = _search_windows_registry_for_stm32cubeprogrammer()
        if not self.exe:
            for path in self._FALLBACK_PATHS:
                if os.path.isfile(path):
                    self.exe = path
                    break

    @staticmethod
    def available():
        return CubeProgrammerCLI().exe is not None

    def list_probes(self):
        """Returns a list of serial numbers found via `-l` ("Connected
        ST-LINK Probes List", one "ST-LINK SN : <serial>" line per probe -
        confirmed against real ST community-forum output). Returns [] if
        none found, the tool isn't available, or the output didn't match
        the expected format (a different CubeProgrammer version phrasing
        this differently would show up as an empty list here rather than
        a wrong guess)."""
        if not self.exe:
            return []
        try:
            output = self._run(["-l"], dry_run=False, check_output=False)
        except SWDFlashError:
            return []
        # \S+ (any non-whitespace), not a hex-only character class - some
        # ST-Link/V2-1 and STLINK-V3 variants (Discovery/Nucleo boards
        # especially) report serials that aren't purely hex, and those
        # would otherwise just be silently excluded from the probe list.
        # Hyphen optional in "ST-LINK"/"STLINK" - confirmed spelling has
        # varied between CubeProgrammer versions/platforms.
        serials = re.findall(r"ST-?LINK SN\s*:\s*(\S{6,})", output)
        return serials

    def check_connection(self, serial=None, dry_run=False):
        # A connect-only invocation (-c port=SWD with no -e/-w action flag)
        # should print target/device identification on success and exit.
        # This requires POSITIVE confirmation text, not just the absence of
        # a failure phrase - which is exactly the gap a real test exposed:
        # with no ST-Link connected at all, this tool still logged
        # "complete" for a full erase+program sequence, meaning its exit
        # code alone isn't trustworthy. Failing closed (no confirmation =
        # treated as failure) is the safer default for a destructive tool.
        self.log(_("LOG_CHECKING_PROBE_AND_TARGET"))
        # port and sn (when present) must be one combined string after -c -
        # STM32CubeProgrammer's own syntax integrates every connection
        # parameter into a single argument (e.g. "port=SWD sn=123456"), not
        # separate ones as subprocess would otherwise pass them - sn as its
        # own list element would read to the CLI as an unrelated
        # positional argument rather than part of -c's value, breaking
        # probe selection by serial number.
        connect_args = ["-c", "port=SWD" + (f" sn={serial}" if serial else "")]
        if dry_run:
            self.log(f"$ {self.exe} " + " ".join(connect_args))
            self.log(_("LOG_DRY_RUN_SKIP_CONNECTION"))
            return
        probes = self.list_probes()
        if len(probes) > 1 and not serial:
            raise SWDFlashError(
                f"{len(probes)} ST-Link probes are connected and none was "
                f"selected - refusing to guess which one to run a chip erase "
                f"against. Pick one from the probe list."
            )
        output = self._run(connect_args, dry_run=False, check_output=False)
        lower = output.lower()
        if self._looks_like_failure(output, exclude_args=connect_args):
            raise SWDFlashError(
                "STM32CubeProgrammer's connection check reported a failure - "
                "see the log above. Check the ST-Link is connected via USB "
                "and wired to the chip's SWD pins."
            )
        if not any(hint in lower for hint in ("device id", "board", "chip id", "connected via", "st-link sn")):
            raise SWDFlashError(
                "Couldn't confirm STM32CubeProgrammer actually connected to a "
                "target - its output didn't contain any expected connection-"
                "confirmation text (see the log above for exactly what it did "
                "print). Not proceeding without that confirmation. Check the "
                "ST-Link is connected via USB and wired to the chip's SWD pins."
            )
        self.log(_("LOG_CONNECTION_CONFIRMED"))

    def backup_flash(self, output_path, dry_run=False, serial=None):
        """Reads the entire 256KB flash region (bootloader + application +
        whatever's between/after) to output_path before any erase touches
        it - a full-chip flash is the one operation in this tool that
        can't be undone by re-running it (unlike a CAN OTA update, which
        the golden-image backup slot protects against), so a local backup
        beforehand is real, meaningful insurance, not just a formality.
        Uses CubeProgrammer's -r <address> <size> <file> memory-read-to-file
        syntax (confirmed against ST's own community documentation - -r32
        with the same 3 arguments only dumps to the screen, never to a
        file; it silently produced no output file at all here before).
        Returns True if the backup file was actually written and
        is non-empty, False otherwise (including for a dry run, where
        nothing is written at all) - full_chip_flash uses this to decide
        whether it's safe to proceed with the erase that follows."""
        connect = ["-c", "port=SWD" + (f" sn={serial}" if serial else "")]
        self.log(_("LOG_BACKING_UP_FLASH", path=output_path))
        if dry_run:
            self.log(f"$ {self.exe} " + " ".join(connect) + f" -r 0x08000000 0x40000 \"{output_path}\"")
            self.log(_("LOG_DRY_RUN_NOTHING_TO_VERIFY"))
            return False
        self._run(connect + ["-r", "0x08000000", "0x40000", output_path], dry_run=False)
        ok = os.path.isfile(output_path) and os.path.getsize(output_path) > 0
        if ok:
            self.log(_("LOG_BACKUP_WRITTEN", path=output_path, size=os.path.getsize(output_path)))
        else:
            self.log(_("LOG_BACKUP_NO_FILE"))
        return ok

    def full_chip_flash(self, bootloader_path, app_path, dry_run=False, serial=None,
                         backup_path=None):
        self.check_connection(serial, dry_run)
        connect = ["-c", "port=SWD" + (f" sn={serial}" if serial else "")]
        if backup_path is not None:
            if not self.backup_flash(backup_path, dry_run=dry_run, serial=serial) and not dry_run:
                raise SWDFlashError(
                    f"Backup to {backup_path} didn't produce a usable file - stopping before "
                    f"the erase rather than proceeding without it. Retry without a backup path "
                    f"if you're confident you don't need one this time."
                )
        self.log(_("LOG_CUBE_FULLCHIP_HEADER"))
        self.log(_("LOG_ERASING_ENTIRE_CHIP"))
        self._run(connect + ["-e", "all"], dry_run)

        self.log(_("LOG_PROGRAMMING_BOOTLOADER_REGION"))
        args = connect + ["-w", bootloader_path]
        args += _address_args_bin_needs_it(bootloader_path, BOOTLOADER_FLASH_ADDR)
        self._run(args, dry_run)

        self.log(_("LOG_PROGRAMMING_APP_VERIFY_RESET"))
        args = connect + ["-w", app_path]
        args += _address_args_bin_needs_it(app_path, APP_FLASH_ADDR)
        args += ["-v", "-rst"]
        self._run(args, dry_run)
        self.log(_("LOG_CUBE_FULLCHIP_COMPLETE"))

    def read_option_bytes(self, serial=None, dry_run=False):
        """Read-only option byte dump ('-ob displ') - no erase, no write,
        just a connect-and-report. Primarily meant to flag RDP2 before it's
        too late to matter: RDP0/RDP1 are both reversible (RDP1 -> RDP0
        works via -rdu, which does a mass-erase but leaves the debug port
        usable again), but RDP2 is a permanent, intentionally irreversible
        lock-out by ST's own design - this project's SWD section has been
        careful throughout to distinguish "recoverable via SWD" from that
        one true exception, and this check exists to catch it BEFORE a
        full-chip operation, not after.

        Returns (rdp_level_str_or_None, raw_output) - rdp_level_str is
        "0"/"1"/"2" when confidently identified, None if the output didn't
        contain a recognizable RDP indicator (shown to the user rather than
        guessed at).
        """
        self.log(_("LOG_READING_OPTION_BYTES_HEADER"))
        connect = ["-c", "port=SWD" + (f" sn={serial}" if serial else "")]
        if dry_run:
            self.log(f"$ {self.exe} " + " ".join(connect) + " -ob displ")
            self.log(_("LOG_DRY_RUN_NOT_EXECUTED"))
            return None, ""
        output = self._run(connect + ["-ob", "displ"], dry_run=False, check_output=False)
        # RDP is checked BEFORE the generic failure-text check: CubeProgrammer
        # genuinely phrases the RDP1 case as starting with "Error: RDP level
        # is set to 1..." (confirmed against real ST community-forum output)
        # - that's a successful, meaningful answer to "what's the RDP
        # level", not a connection failure, even though it contains the
        # word "Error". Finding a recognizable RDP signal at all is itself
        # evidence the read worked.
        lower = output.lower()
        rdp_level = None
        # Proximity-bound rather than whole-output substring checks - "rdp"
        # and the hex value must appear within ~40 characters of each
        # other, not just both present somewhere in potentially unrelated
        # parts of the output (a WRP register or a memory address
        # containing the same hex substring, with "rdp" appearing
        # completely separately elsewhere, would otherwise still match).
        if re.search(r"rdp.{0,40}0xcc|0xcc.{0,40}rdp", lower, re.DOTALL):
            rdp_level = "2"
        elif re.search(r"rdp.{0,40}0xaa|0xaa.{0,40}rdp", lower, re.DOTALL):
            rdp_level = "0"
        elif re.search(r"rdp.{0,40}0xbb|0xbb.{0,40}rdp", lower, re.DOTALL):
            rdp_level = "1"
        else:
            m = re.search(r"rdp\D{0,15}level\D{0,10}(\d)", lower)
            if m:
                rdp_level = m.group(1)

        if rdp_level is None and self._looks_like_failure(output, exclude_args=connect):
            raise SWDFlashError(
                "Couldn't read option bytes - see the log above. Check the "
                "ST-Link is connected via USB and wired to the chip's SWD pins."
            )
        rdp_display = rdp_level if rdp_level else _("LBL_COULD_NOT_PARSE_RDP")
        self.log(_("LOG_RDP_LEVEL_DETECTED", level=rdp_display))
        return rdp_level, output


