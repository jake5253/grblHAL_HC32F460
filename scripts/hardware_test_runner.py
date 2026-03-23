#!/usr/bin/env python3
"""
Guided hardware test runner for the HC32F460 grblHAL bench checklist.

This automates the safe serial-driven parts of the checklist and walks the user
through manual tests while capturing results in a timestamped markdown log.

Requires:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import select
import subprocess
import sys
import termios
import textwrap
import time
import tty
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

try:
    import serial
    from serial import SerialException
except ImportError:  # pragma: no cover - dependency check only
    serial = None
    SerialException = Exception


ROOT = Path("/home/j/Development/grblHAL")
DRIVER_DIR = ROOT / "HC32F460"
BUILD_DIR = DRIVER_DIR / "build"
DEFAULT_OUTPUT_DIR = ROOT / "HC32F460" / "test_logs"
DEFAULT_BAUD = 115200
READ_CHUNK_DELAY = 0.05


def now_str() -> str:
    return dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def slug_now() -> str:
    return dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def prompt(text: str, default: Optional[str] = None) -> str:
    suffix = f" [{default}]" if default is not None else ""
    value = input(f"{text}{suffix}: ").strip()
    return value if value else (default or "")


def confirm(text: str, default: bool = True) -> bool:
    hint = "Y/n" if default else "y/N"
    while True:
        value = input(f"{text} [{hint}]: ").strip().lower()
        if not value:
            return default
        if value in {"y", "yes"}:
            return True
        if value in {"n", "no"}:
            return False
        print("Enter y or n.")


def choose_result() -> str:
    while True:
        value = input("Result [p=pass, f=fail, s=skip]: ").strip().lower()
        if value in {"p", "pass"}:
            return "PASS"
        if value in {"f", "fail"}:
            return "FAIL"
        if value in {"s", "skip"}:
            return "SKIP"
        print("Enter p, f, or s.")


def wrap_block(text: str) -> str:
    return "\n".join(textwrap.wrap(text, width=100)) if text else ""


def run_command(args: List[str], cwd: Optional[Path] = None) -> Optional[str]:
    try:
        result = subprocess.run(
            args,
            cwd=str(cwd) if cwd else None,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.SubprocessError, FileNotFoundError):
        return None
    return result.stdout.strip()


def find_git_root() -> Optional[Path]:
    candidates = [DRIVER_DIR, ROOT, ROOT.parent]
    for candidate in candidates:
        git_root = run_command(["git", "rev-parse", "--show-toplevel"], cwd=candidate)
        if git_root:
            return Path(git_root)
    return None


@dataclass
class GitState:
    root: Optional[Path]
    commit: str
    branch: str
    dirty: bool

    @property
    def tree(self) -> str:
        return "dirty" if self.dirty else "clean"

    def describe(self) -> str:
        if self.root is None:
            return "not a git repo"
        return f"{self.commit} ({self.branch}, {self.tree})"


@dataclass
class FirmwareIdentity:
    raw: str
    build_info: str
    commit: Optional[str]
    branch: Optional[str]
    tree: Optional[str]


def detect_git_state() -> GitState:
    git_root = find_git_root()
    if git_root is None:
        return GitState(root=None, commit="unknown", branch="unknown", dirty=False)

    commit = run_command(["git", "rev-parse", "--short", "HEAD"], cwd=git_root) or "unknown"
    status = run_command(["git", "status", "--short"], cwd=git_root) or ""
    branch = run_command(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=git_root) or "unknown"
    return GitState(root=git_root, commit=commit, branch=branch, dirty=bool(status))


def parse_firmware_identity(text: str) -> Optional[FirmwareIdentity]:
    for line in text.splitlines():
        match = re.search(r"\[VER:[^:]+:(.*?)\]", line)
        if not match:
            continue

        build_info = match.group(1)
        commit_match = re.search(r"(?:^|\|)git=([^|\]]+)", build_info)
        branch_match = re.search(r"(?:^|\|)branch=([^|\]]+)", build_info)
        tree_match = re.search(r"(?:^|\|)tree=([^|\]]+)", build_info)

        return FirmwareIdentity(
            raw=line,
            build_info=build_info,
            commit=commit_match.group(1) if commit_match else None,
            branch=branch_match.group(1) if branch_match else None,
            tree=tree_match.group(1) if tree_match else None,
        )

    return None


def detect_firmware_artifacts() -> str:
    artifacts = [BUILD_DIR / "firmware.elf", BUILD_DIR / "firmware.bin"]
    found = []
    for artifact in artifacts:
        if artifact.exists():
            stat = artifact.stat()
            stamp = dt.datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
            found.append(f"{artifact.name} @ {stamp}")
    return ", ".join(found) if found else "no build artifacts found"


def detect_default_serial_console(build_flags: str) -> str:
    if "USE_USART=1" in build_flags.replace(" ", ""):
        return "alternate USART1"
    return "default USART2"


def detect_defaults(args: argparse.Namespace) -> dict:
    build_flags = args.build_flags or os.environ.get("PLATFORMIO_BUILD_FLAGS", "")
    git_state = detect_git_state()
    defaults = {
        "Date": now_str(),
        "Firmware commit / tree state": git_state.describe(),
        "Build target": args.build_target or "hc32f460_voxelab_aquila_v102",
        "Build flags": build_flags,
        "Flash method": args.flash_method or "pyocd / platformio",
        "Serial console path": args.serial_console or detect_default_serial_console(build_flags),
        "Board wiring notes": "",
        "PSU voltage": "24V",
        "Motors connected": "yes",
        "Limit switches connected": "yes",
        "Probe connected": "yes",
        "Spindle connected": "no",
        "Laser connected": "no",
        "SD card inserted": "no",
        "Notes": "",
        "Detected firmware artifacts": detect_firmware_artifacts(),
    }
    return defaults


@dataclass
class StepLog:
    title: str
    result: str
    notes: str = ""
    commands: List[str] = field(default_factory=list)
    responses: List[str] = field(default_factory=list)


class SerialSession:
    def __init__(self, port: str, baud: int, timeout: float = 0.25) -> None:
        if serial is None:
            raise RuntimeError("pyserial is not installed. Run: pip install pyserial")
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=timeout)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    def close(self) -> None:
        self.ser.close()

    def read_available(self, duration: float = 0.6) -> str:
        end = time.time() + duration
        chunks: List[str] = []
        while time.time() < end:
            waiting = self.ser.in_waiting
            if waiting:
                chunks.append(self.ser.read(waiting).decode(errors="replace"))
                time.sleep(READ_CHUNK_DELAY)
                continue
            time.sleep(READ_CHUNK_DELAY)
        return "".join(chunks).strip()

    def send_line(self, line: str, settle: float = 0.6) -> str:
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        return self.read_available(duration=settle)

    def soft_reset(self, settle: float = 1.0) -> str:
        self.ser.write(b"\x18")
        self.ser.flush()
        return self.read_available(duration=settle)

    def send_raw(self, data: bytes, settle: float = 0.3) -> str:
        self.ser.write(data)
        self.ser.flush()
        return self.read_available(duration=settle)


class HardwareTestRunner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.steps: List[StepLog] = []
        self.session_info = {}
        self.serial_session: Optional[SerialSession] = None
        self.output_dir = Path(args.output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.log_path = self.output_dir / f"hc32f460_test_run_{slug_now()}.md"
        self.defaults = detect_defaults(args)
        self.git_state = detect_git_state()
        self.firmware_identity: Optional[FirmwareIdentity] = None

    def run(self) -> int:
        if self.args.show_sender_commands:
            print_sender_commands()
            return 0

        if self.args.console:
            return run_console(self.args.port, self.args.baud)

        self.collect_session_info()
        if not confirm("Proceed with the hardware test session?", True):
            print("Aborted.")
            return 1

        try:
            self.open_serial()
            if not self.validate_firmware_state():
                return 3
            self.run_steps()
        except (RuntimeError, SerialException) as exc:
            print(f"Error: {exc}", file=sys.stderr)
            return 2
        finally:
            if self.serial_session is not None:
                self.serial_session.close()
            self.write_log()

        print(f"\nWrote test log: {self.log_path}")
        return 0

    def collect_session_info(self) -> None:
        print("HC32F460 Hardware Test Runner\n")
        self.session_info = {
            "Date": self.defaults["Date"],
            "Firmware commit / tree state": prompt("Firmware commit / tree state", self.defaults["Firmware commit / tree state"]),
            "Build target": prompt("Build target", self.defaults["Build target"]),
            "Build flags": prompt("Build flags", self.defaults["Build flags"]),
            "Flash method": prompt("Flash method", self.defaults["Flash method"]),
            "Serial console path": prompt("Serial console path", self.defaults["Serial console path"]),
            "Board wiring notes": prompt("Board wiring notes", self.defaults["Board wiring notes"]),
            "PSU voltage": prompt("PSU voltage", self.defaults["PSU voltage"]),
            "Motors connected": prompt("Motors connected", self.defaults["Motors connected"]),
            "Limit switches connected": prompt("Limit switches connected", self.defaults["Limit switches connected"]),
            "Probe connected": prompt("Probe connected", self.defaults["Probe connected"]),
            "Spindle connected": prompt("Spindle connected", self.defaults["Spindle connected"]),
            "Laser connected": prompt("Laser connected", self.defaults["Laser connected"]),
            "SD card inserted": prompt("SD card inserted", self.defaults["SD card inserted"]),
            "Detected firmware artifacts": self.defaults["Detected firmware artifacts"],
            "Notes": prompt("Session notes", self.defaults["Notes"]),
        }

    def open_serial(self) -> None:
        port = self.args.port or prompt("Serial port", "/tmp/ttyESP0")
        baud = self.args.baud
        print(f"\nOpening serial port {port} @ {baud}...")
        self.serial_session = SerialSession(port, baud)
        banner = self.serial_session.read_available(duration=1.2)
        self.steps.append(StepLog("Initial banner capture", "INFO", responses=[banner]))
        if banner:
            print("\nStartup banner:")
            print(banner)

    def validate_firmware_state(self) -> bool:
        assert self.serial_session is not None

        firmware_response = self.serial_session.send_line("$I", settle=1.0)
        self.steps.append(
            StepLog("Installed firmware identity", "INFO", commands=["$I"], responses=[firmware_response])
        )
        self.firmware_identity = parse_firmware_identity(firmware_response)

        installed = self.firmware_identity.build_info if self.firmware_identity else "unparseable"
        self.session_info["Installed firmware build info"] = installed

        if self.firmware_identity is None:
            print("\nUnable to parse firmware build info from $I.")
            print("Flash a current firmware build before running the checklist.")
            return False

        if self.git_state.root is None:
            print("\nCurrent HC32F460 git state could not be determined.")
            print("The runner cannot verify firmware freshness, so it is stopping.")
            return False

        mismatch = self.firmware_identity.commit != self.git_state.commit
        dirty_tree = self.git_state.dirty

        if dirty_tree and not self.args.allow_dirty_tree:
            print("\nCurrent HC32F460 repo tree is dirty.")
            print(f"Repo state: {self.git_state.describe()}")
            print(f"Installed firmware: {installed}")
            print("Build and flash a clean commit, or rerun with --allow-dirty-tree.")
            return False

        if mismatch and not self.args.allow_firmware_mismatch:
            print("\nInstalled firmware commit does not match the current HC32F460 repo commit.")
            print(f"Repo state: {self.git_state.describe()}")
            print(f"Installed firmware: {installed}")
            print("Rebuild and reflash before running the checklist, or rerun with --allow-firmware-mismatch.")
            return False

        return True

    def run_steps(self) -> None:
        self.step_boot_console()
        self.step_eeprom_persistence()
        self.step_idle_inputs()
        self.step_motion_basics()
        self.step_limits_homing()
        self.step_probe()
        self.step_motion_stability()
        self.step_primary_spindle()
        self.step_secondary_spindle_laser()
        self.step_laser_specific()
        self.step_optional_controls()
        self.step_alt_uart()

    def serial_command_step(self, title: str, commands: List[str], settle: float = 0.7) -> None:
        assert self.serial_session is not None
        responses: List[str] = []
        print(f"\n{title}")
        for command in commands:
            print(f"  -> {command}")
            responses.append(self.serial_session.send_line(command, settle=settle))
        result = choose_result()
        notes = prompt("Notes", "")
        self.steps.append(StepLog(title, result, notes, commands, responses))

    def interactive_step(self, title: str, guidance: str) -> None:
        print(f"\n{title}")
        print(textwrap.indent(wrap_block(guidance), prefix="  "))
        result = choose_result()
        notes = prompt("Notes", "")
        self.steps.append(StepLog(title, result, notes))

    def step_boot_console(self) -> None:
        self.serial_command_step(
            "Boot And Console",
            ["$I", "$$", "?", "$pins"],
            settle=0.9,
        )

    def step_eeprom_persistence(self) -> None:
        assert self.serial_session is not None
        if not confirm("\nRun EEPROM/settings persistence check now?", False):
            self.steps.append(StepLog("EEPROM / Settings Persistence", "SKIP", "Skipped by user."))
            return

        setting = prompt("Harmless setting command to change", "$10=3")
        verify = prompt("Verification command", "$$")
        commands = [setting, verify]
        responses = [self.serial_session.send_line(cmd, settle=0.8) for cmd in commands]
        print("Now soft reset the controller.")
        responses.append(self.serial_session.soft_reset(settle=1.0))
        print("Power-cycle the board if you want full persistence verification, then press Enter.")
        input()
        responses.append(self.serial_session.send_line(verify, settle=0.8))
        result = choose_result()
        notes = prompt("Notes", "")
        self.steps.append(
            StepLog("EEPROM / Settings Persistence", result, notes, commands + ["Ctrl-X", verify], responses)
        )

    def step_idle_inputs(self) -> None:
        self.serial_command_step("Idle Input Sanity", ["$pins", "?"], settle=0.8)

    def step_motion_basics(self) -> None:
        guidance = (
            "Jog X, Y, and Z in both directions at low speed. Verify direction, smoothness, and that the "
            "mechanics are clear. Record any inversion, roughness, stalls, chatter, or timer issues."
        )
        self.interactive_step("Motion Basics", guidance)

    def step_limits_homing(self) -> None:
        guidance = (
            "Manually trigger X, Y, and Z limits and observe $pins. Then test homing and, when safe, "
            "hard-limit trips during slow motion. Watch for wrong polarity, false triggers, or incorrect "
            "axis mapping."
        )
        self.interactive_step("Homing And Limits", guidance)

    def step_probe(self) -> None:
        assert self.serial_session is not None
        if not confirm("\nRun probe checks now?", True):
            self.steps.append(StepLog("Probe Input", "SKIP", "Skipped by user."))
            return

        commands = ["$pins", "?"]
        responses = [self.serial_session.send_line(cmd, settle=0.8) for cmd in commands]
        guidance = (
            "Manually trigger the PA4 probe and observe $pins, then run one or more cautious low-speed probe "
            "cycles from your normal sender. Record polarity and repeatability."
        )
        print("\nProbe Input")
        print(textwrap.indent(wrap_block(guidance), prefix="  "))
        result = choose_result()
        notes = prompt("Notes", "")
        self.steps.append(StepLog("Probe Input", result, notes, commands, responses))

    def step_motion_stability(self) -> None:
        guidance = (
            "Run repeated jogs, longer moves, repeated homing, and a powered idle soak. Then retest motion "
            "and note any lockups, unexpected alarms, or heat-related changes."
        )
        self.interactive_step("Extended Motion Stability", guidance)

    def step_primary_spindle(self) -> None:
        assert self.serial_session is not None
        if not confirm("\nRun primary spindle output test now?", False):
            self.steps.append(StepLog("Primary Spindle Test", "SKIP", "Skipped by user."))
            return

        commands = ["M5", "M3 S100", "M3 S500", "M3 S1000", "M5"]
        self.serial_command_step("Primary Spindle Test", commands, settle=0.9)

    def step_secondary_spindle_laser(self) -> None:
        assert self.serial_session is not None
        if not confirm("\nRun secondary spindle / laser output test now?", False):
            self.steps.append(StepLog("Secondary Spindle / Laser Test", "SKIP", "Skipped by user."))
            return

        commands = ["M5", "M4 P1", "M4 S10", "M4 S100", "M5"]
        self.serial_command_step("Secondary Spindle / Laser Test", commands, settle=0.9)

    def step_laser_specific(self) -> None:
        assert self.serial_session is not None
        if not confirm("\nRun laser-specific checks now?", False):
            self.steps.append(StepLog("Laser-Specific Checks", "SKIP", "Skipped by user."))
            return

        commands = ["$32=1", "M4 S1", "M5", "$32=0"]
        self.serial_command_step("Laser-Specific Checks", commands, settle=0.9)

    def step_optional_controls(self) -> None:
        guidance = (
            "Only run this if CONTROL_ENABLE was compiled in and the PB12/PB13/PB14 inputs are wired with "
            "proper external conditioning. Verify halt/e-stop, feed-hold, and cycle-start in idle and during motion."
        )
        self.interactive_step("Optional Control Inputs", guidance)

    def step_alt_uart(self) -> None:
        guidance = (
            "Only run this if you flashed a USE_USART=1 build. Verify PC0/PC1 console stability, startup banner, "
            "command/response integrity, and longer command-stream reliability."
        )
        self.interactive_step("Optional Alternate Console UART", guidance)

    def write_log(self) -> None:
        lines: List[str] = []
        lines.append("# HC32F460 Hardware Test Run")
        lines.append("")
        lines.append("## Session Record")
        lines.append("")
        for key, value in self.session_info.items():
            lines.append(f"- {key}: {value}")
        lines.append("")
        lines.append("## Step Results")
        lines.append("")
        for step in self.steps:
            lines.append(f"### {step.title}")
            lines.append("")
            lines.append(f"- Result: {step.result}")
            if step.notes:
                lines.append(f"- Notes: {step.notes}")
            if step.commands:
                lines.append("- Commands:")
                for command in step.commands:
                    lines.append(f"  - `{command}`")
            if step.responses:
                lines.append("- Responses:")
                for idx, response in enumerate(step.responses, start=1):
                    lines.append(f"  - Response {idx}:")
                    lines.append("")
                    lines.append("    ```text")
                    for line in (response or "<no response>").splitlines():
                        lines.append(f"    {line}")
                    lines.append("    ```")
            lines.append("")

        self.log_path.write_text("\n".join(lines))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="HC32F460 grblHAL hardware test runner")
    parser.add_argument("--port", help="Serial port to use, e.g. /tmp/ttyESP0 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baud rate, default {DEFAULT_BAUD}")
    parser.add_argument("--build-target", help="Default build target to record in the session log")
    parser.add_argument("--build-flags", help="Default build flags to record in the session log")
    parser.add_argument("--flash-method", help="Default flash method to record in the session log")
    parser.add_argument("--serial-console", help="Default serial console path description to record in the session log")
    parser.add_argument(
        "--console",
        action="store_true",
        help="Open a simple interactive serial console instead of the guided checklist runner",
    )
    parser.add_argument(
        "--show-sender-commands",
        action="store_true",
        help="Print a compact set of suggested bench-test commands and exit",
    )
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help=f"Directory for test logs, default {DEFAULT_OUTPUT_DIR}",
    )
    parser.add_argument(
        "--allow-dirty-tree",
        action="store_true",
        help="Allow the checklist to proceed even when the current HC32F460 repo tree is dirty",
    )
    parser.add_argument(
        "--allow-firmware-mismatch",
        action="store_true",
        help="Allow the checklist to proceed even when flashed firmware commit does not match the current repo",
    )
    return parser.parse_args()


def print_sender_commands() -> None:
    print(
        textwrap.dedent(
            """
            HC32F460 sender-friendly bench commands

            Basic status:
              $I
              $$
              ?
              $pins

            Motion sanity:
              G91
              $J=G21G91X1F100
              $J=G21G91X-1F100
              $J=G21G91Y1F100
              $J=G21G91Y-1F100
              $J=G21G91Z1F60
              $J=G21G91Z-1F60
              G90

            Homing / limits:
              $H
              $X
              $pins

            Probe:
              G91
              G38.2 Z-5 F25
              G90

            Primary spindle:
              M5
              M3 S100
              M3 S500
              M3 S1000
              M5

            Secondary spindle / laser:
              M5
              M4 P1
              M4 S10
              M4 S100
              M5

            Laser mode:
              $32=1
              M4 S1
              M5
              $32=0

            Soft reset:
              Ctrl-X
            """
        ).strip()
    )


def run_console(port_arg: Optional[str], baud: int) -> int:
    port = port_arg or prompt("Serial port", "/tmp/ttyESP0")
    try:
        session = SerialSession(port, baud)
    except (RuntimeError, SerialException) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    print(f"Connected to {port} @ {baud}")
    banner = session.read_available(duration=1.2)
    if banner:
        print("\nStartup banner:")
        print(banner)

    print(
        textwrap.dedent(
            """
            Interactive console commands:
              :help       show console help
              :reset      send Ctrl-X soft reset
              Ctrl-X      send immediate soft reset without pressing Enter
              :status     send ?
              :pins       send $pins
              :sender     show suggested bench-test commands
              :quit       exit console

            Any other line is sent directly to the controller.
            """
        ).strip()
    )

    stdin_fd = sys.stdin.fileno()
    old_attrs = termios.tcgetattr(stdin_fd)
    line = ""

    def redraw_prompt() -> None:
        sys.stdout.write("\r\033[2Khc32> " + line)
        sys.stdout.flush()

    def print_response(response: str) -> None:
        sys.stdout.write("\r\033[2K")
        if response:
            sys.stdout.write(response)
            if not response.endswith("\n"):
                sys.stdout.write("\n")
        else:
            sys.stdout.write("<no response>\n")
        redraw_prompt()

    try:
        tty.setcbreak(stdin_fd)
        redraw_prompt()

        while True:
            ready, _, _ = select.select([stdin_fd, session.ser.fileno()], [], [])

            if session.ser.fileno() in ready:
                incoming = session.read_available(duration=0.05)
                if incoming:
                    sys.stdout.write("\r\033[2K" + incoming)
                    if not incoming.endswith("\n"):
                        sys.stdout.write("\n")
                    redraw_prompt()

            if stdin_fd not in ready:
                continue

            ch = os.read(stdin_fd, 1)
            if not ch:
                break

            if ch == b"\x18":  # Ctrl-X
                response = session.soft_reset(settle=1.0)
                sys.stdout.write("\r\033[2K[soft reset sent]\n")
                print_response(response)
                line = ""
                redraw_prompt()
                continue

            if ch in {b"\x03", b"\x04"}:  # Ctrl-C / Ctrl-D
                sys.stdout.write("\n")
                break

            if ch in {b"\r", b"\n"}:
                command = line.strip()
                line = ""
                sys.stdout.write("\n")
                sys.stdout.flush()

                if not command:
                    redraw_prompt()
                    continue
                if command == ":quit":
                    break
                if command == ":help":
                    print("Use Ctrl-X, :reset, :status, :pins, :sender, or :quit. Other text is sent as a command.")
                    redraw_prompt()
                    continue
                if command == ":sender":
                    print_sender_commands()
                    redraw_prompt()
                    continue
                if command == ":reset":
                    response = session.soft_reset(settle=1.0)
                elif command == ":status":
                    response = session.send_line("?", settle=0.6)
                elif command == ":pins":
                    response = session.send_line("$pins", settle=0.8)
                elif command.startswith(":raw "):
                    payload = command[len(":raw "):].encode()
                    response = session.send_raw(payload, settle=0.5)
                else:
                    response = session.send_line(command, settle=0.8)

                print_response(response)
                continue

            if ch in {b"\x7f", b"\x08"}:
                if line:
                    line = line[:-1]
                    redraw_prompt()
                continue

            if ch.isascii() and ch >= b" ":
                line += ch.decode(errors="ignore")
                redraw_prompt()
    finally:
        termios.tcsetattr(stdin_fd, termios.TCSADRAIN, old_attrs)
        session.close()

    return 0


def main() -> int:
    args = parse_args()
    runner = HardwareTestRunner(args)
    return runner.run()


if __name__ == "__main__":
    raise SystemExit(main())
