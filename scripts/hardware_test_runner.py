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
import glob
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
    import tkinter as tk
    from tkinter import ttk
    from tkinter.scrolledtext import ScrolledText
except ImportError:  # pragma: no cover - GUI optional
    tk = None
    ttk = None
    ScrolledText = None

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
FIRMWARE_PATHS = (
    "Inc",
    "Src",
    "boards",
    "SDK",
    "grbl",
    "ldscripts",
    "platformio.ini",
    "driver.json",
    "scripts/platformio_pre.py",
    "scripts/build-hc32f460.sh",
)
BENCH_SETTING_IDS = (
    0, 1, 2, 3, 4, 5, 6,
    21, 22, 23, 24, 25, 26, 27, 32,
    100, 101, 102, 110, 111, 112, 120, 121, 122, 130, 131, 132,
    395,
)
MASK_SETTING_IDS = (2, 3, 5, 23)
SETTING_LABELS = {
    0: "Step pulse time (us)",
    1: "Step idle delay (ms)",
    2: "Step pulse invert",
    3: "Step direction invert",
    4: "Invert stepper enable outputs",
    5: "Invert limit inputs",
    6: "Invert probe inputs",
    21: "Hard limits enable",
    22: "Homing cycle enable",
    23: "Homing direction invert",
    24: "Homing locate feed rate (mm/min)",
    25: "Homing search seek rate (mm/min)",
    26: "Homing switch debounce delay (ms)",
    27: "Homing switch pull-off distance (mm)",
    32: "Laser mode / mode of operation",
    100: "X-axis travel resolution (step/mm)",
    101: "Y-axis travel resolution (step/mm)",
    102: "Z-axis travel resolution (step/mm)",
    110: "X-axis maximum rate (mm/min)",
    111: "Y-axis maximum rate (mm/min)",
    112: "Z-axis maximum rate (mm/min)",
    120: "X-axis acceleration (mm/sec^2)",
    121: "Y-axis acceleration (mm/sec^2)",
    122: "Z-axis acceleration (mm/sec^2)",
    130: "X-axis maximum travel (mm)",
    131: "Y-axis maximum travel (mm)",
    132: "Z-axis maximum travel (mm)",
    395: "Default spindle selection",
}


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


def prompt_yes_no(text: str, default: str = "yes") -> str:
    default_bool = default.strip().lower() in {"y", "yes", "true", "1"}
    return "yes" if confirm(text, default_bool) else "no"


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


def parse_setting_value(text: str, setting_id: int) -> Optional[str]:
    match = re.search(rf"^\${setting_id}=([^\r\n]+)$", text, re.MULTILINE)
    return match.group(1).strip() if match else None


def parse_settings_dump(text: str) -> dict[int, str]:
    settings: dict[int, str] = {}
    for match in re.finditer(r"^\$(\d+)=([^\r\n]+)$", text, re.MULTILINE):
        settings[int(match.group(1))] = match.group(2).strip()
    return settings


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


def is_firmware_tree_dirty(git_root: Optional[Path]) -> bool:
    if git_root is None:
        return False

    status = run_command(["git", "status", "--short", "--", *FIRMWARE_PATHS], cwd=git_root) or ""
    return bool(status)


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


def detect_build_target() -> str:
    if os.environ.get("PIOENV"):
        return os.environ["PIOENV"]
    return "hc32f460_voxelab_aquila_v102"


def detect_build_flags() -> str:
    flags = os.environ.get("PLATFORMIO_BUILD_FLAGS", "").strip()
    return flags or "default"


def detect_default_serial_console(build_flags: str) -> str:
    if "USE_USART=1" in build_flags.replace(" ", ""):
        return "alternate USART1"
    return "default USART2"


def detect_serial_port_candidate() -> Optional[str]:
    if os.environ.get("GRBL_SERIAL_PORT"):
        return os.environ["GRBL_SERIAL_PORT"]

    candidates = []

    for pattern in (
        "/dev/serial/by-id/*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
        "/dev/tty.usbserial*",
        "/dev/cu.usbserial*",
        "/tmp/ttyESP0",
    ):
        candidates.extend(sorted(glob.glob(pattern)))

    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate

    return None


def detect_defaults(args: argparse.Namespace) -> dict:
    build_flags = args.build_flags or detect_build_flags()
    git_state = detect_git_state()
    defaults = {
        "Date": now_str(),
        "Firmware commit / tree state": git_state.describe(),
        "Build target": args.build_target or detect_build_target(),
        "Build flags": build_flags,
        "Flash method": args.flash_method or "pyocd / platformio",
        "Serial console path": args.serial_console or detect_default_serial_console(build_flags),
        "Detected serial port": args.port or detect_serial_port_candidate() or "not detected",
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
        text = "".join(chunks).replace("\r\n", "\n").replace("\r", "\n")
        return text.strip()

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


class BenchControlGui:
    def __init__(self, runner: "HardwareTestRunner") -> None:
        if tk is None or ttk is None or ScrolledText is None:
            raise RuntimeError("tkinter is not available in this Python environment.")

        self.runner = runner
        self.root = tk.Tk()
        self.root.title("HC32F460 Bench Control")
        self.root.geometry("1280x860")
        self.root.protocol("WM_DELETE_WINDOW", self._finish)
        self.root.bind("<Control-x>", lambda _event: self._soft_reset())
        self.root.bind("<Control-X>", lambda _event: self._soft_reset())
        self.step_index = 0
        self.current_step_notes = tk.StringVar()
        self.command_var = tk.StringVar()
        self.status_var = tk.StringVar(value="status: not queried")
        self.pins_var = tk.StringVar(value="pins: not queried")
        self.jog_distance_var = tk.StringVar(value="1.0")
        self.jog_feed_var = tk.StringVar(value="100.0")
        self.setting_vars: dict[int, tk.StringVar] = {sid: tk.StringVar() for sid in BENCH_SETTING_IDS}
        self.mask_vars: dict[int, list[tk.IntVar]] = {
            sid: [tk.IntVar(value=0), tk.IntVar(value=0), tk.IntVar(value=0)] for sid in MASK_SETTING_IDS
        }
        self.original_settings: dict[int, str] = {}
        self.steps = [
            (
                "Motion Basics",
                "Use the jog controls to move X, Y, and Z in both directions at low speed. "
                "Verify direction, smoothness, and that the mechanics are clear.",
            ),
            (
                "Homing And Limits",
                "Use $pins, Status, and your normal physical switches to verify limit state. "
                "Run homing and slow hard-limit trips when safe.",
            ),
            (
                "Probe Input",
                "Trigger the probe manually and use $pins to verify polarity. Then run cautious low-speed probe cycles.",
            ),
            (
                "Extended Motion Stability",
                "Run repeated jogs, longer moves, and repeated homing. Watch for lockups, alarms, or heat-related changes.",
            ),
            (
                "Primary Spindle Test",
                "Use the command box or buttons to run M3/M5 tests and verify PWM behavior on spindle 0.",
            ),
            (
                "Secondary Spindle / Laser Test",
                "Use the command box or buttons to run M4/M5 tests and verify spindle 1 / laser output behavior.",
            ),
            (
                "Laser-Specific Checks",
                "Enable laser mode, test low-power output only, and verify safe idle/off behavior.",
            ),
            (
                "Optional Control Inputs",
                "If compiled in and wired safely, verify halt/e-stop, feed-hold, and cycle-start behavior.",
            ),
            (
                "Optional Alternate Console UART",
                "If this is a USE_USART=1 build, verify the alternate UART path and long-run command reliability.",
            ),
        ]
        self._build_ui()
        self._show_step()
        self._schedule_poll()

    def _build_ui(self) -> None:
        self.root.columnconfigure(0, weight=3)
        self.root.columnconfigure(1, weight=2)
        self.root.rowconfigure(0, weight=1)

        left = ttk.Frame(self.root, padding=8)
        left.grid(row=0, column=0, sticky="nsew")
        left.rowconfigure(1, weight=1)
        left.columnconfigure(0, weight=1)

        step_frame = ttk.LabelFrame(left, text="Current Step", padding=8)
        step_frame.grid(row=0, column=0, sticky="ew")
        step_frame.columnconfigure(0, weight=1)
        self.step_title_label = ttk.Label(step_frame, text="", font=("TkDefaultFont", 12, "bold"))
        self.step_title_label.grid(row=0, column=0, sticky="w")
        self.step_body_label = ttk.Label(step_frame, text="", wraplength=700, justify="left")
        self.step_body_label.grid(row=1, column=0, sticky="ew", pady=(6, 0))

        log_frame = ttk.LabelFrame(left, text="Session Log", padding=8)
        log_frame.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        log_frame.rowconfigure(0, weight=1)
        log_frame.columnconfigure(0, weight=1)
        self.log_widget = ScrolledText(log_frame, height=20, wrap="word")
        self.log_widget.grid(row=0, column=0, sticky="nsew")
        self.log_widget.configure(state="disabled")

        prompt_frame = ttk.LabelFrame(left, text="Result / Notes", padding=8)
        prompt_frame.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        prompt_frame.columnconfigure(1, weight=1)
        ttk.Label(prompt_frame, text="Notes").grid(row=0, column=0, sticky="w")
        notes_entry = ttk.Entry(prompt_frame, textvariable=self.current_step_notes)
        notes_entry.grid(row=0, column=1, sticky="ew", padx=(8, 0))
        button_frame = ttk.Frame(prompt_frame)
        button_frame.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Button(button_frame, text="Pass", command=lambda: self._complete_step("PASS")).grid(row=0, column=0, padx=(0, 6))
        ttk.Button(button_frame, text="Fail", command=lambda: self._complete_step("FAIL")).grid(row=0, column=1, padx=6)
        ttk.Button(button_frame, text="Skip", command=lambda: self._complete_step("SKIP")).grid(row=0, column=2, padx=6)
        ttk.Button(button_frame, text="Write Log And Close", command=self._finish).grid(row=0, column=3, padx=(18, 0))

        right = ttk.Frame(self.root, padding=8)
        right.grid(row=0, column=1, sticky="nsew")
        right.rowconfigure(2, weight=1)
        right.columnconfigure(0, weight=1)

        serial_frame = ttk.LabelFrame(right, text="Bench Control", padding=8)
        serial_frame.grid(row=0, column=0, sticky="ew")
        for idx in range(4):
            serial_frame.columnconfigure(idx, weight=1)
        ttk.Button(serial_frame, text="Unlock", command=lambda: self._send_command("$X")).grid(row=0, column=0, sticky="ew", padx=3, pady=3)
        ttk.Button(serial_frame, text="E-Stop / Ctrl-X", command=self._soft_reset).grid(row=0, column=1, sticky="ew", padx=3, pady=3)
        ttk.Button(serial_frame, text="Status", command=self._refresh_status).grid(row=0, column=2, sticky="ew", padx=3, pady=3)
        ttk.Button(serial_frame, text="$pins", command=self._refresh_pins).grid(row=0, column=3, sticky="ew", padx=3, pady=3)
        ttk.Button(serial_frame, text="Home", command=lambda: self._send_command("$H", settle=1.0)).grid(row=1, column=0, sticky="ew", padx=3, pady=3)
        ttk.Button(serial_frame, text="M5", command=lambda: self._send_command("M5")).grid(row=1, column=1, sticky="ew", padx=3, pady=3)
        ttk.Button(serial_frame, text="M3 S1000", command=lambda: self._send_command("M3 S1000")).grid(row=1, column=2, sticky="ew", padx=3, pady=3)
        ttk.Button(serial_frame, text="M4 S100", command=lambda: self._send_command("M4 S100")).grid(row=1, column=3, sticky="ew", padx=3, pady=3)

        command_frame = ttk.LabelFrame(right, text="Manual Command", padding=8)
        command_frame.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        command_frame.columnconfigure(0, weight=1)
        entry = ttk.Entry(command_frame, textvariable=self.command_var)
        entry.grid(row=0, column=0, sticky="ew")
        entry.bind("<Return>", lambda _event: self._send_entry_command())
        ttk.Button(command_frame, text="Send", command=self._send_entry_command).grid(row=0, column=1, padx=(8, 0))

        tabs = ttk.Notebook(right)
        tabs.grid(row=2, column=0, sticky="nsew", pady=(8, 0))

        jog_tab = ttk.Frame(tabs, padding=8)
        state_tab = ttk.Frame(tabs, padding=8)
        settings_tab = ttk.Frame(tabs, padding=8)
        tabs.add(jog_tab, text="Jog")
        tabs.add(state_tab, text="State")
        tabs.add(settings_tab, text="Settings")

        self._build_jog_tab(jog_tab)
        self._build_state_tab(state_tab)
        self._build_settings_tab(settings_tab)

    def _build_jog_tab(self, parent: ttk.Frame) -> None:
        for idx in range(3):
            parent.columnconfigure(idx, weight=1)
        ttk.Label(parent, text="Distance (mm)").grid(row=0, column=0, sticky="w")
        ttk.Entry(parent, textvariable=self.jog_distance_var, width=10).grid(row=0, column=1, sticky="w")
        ttk.Label(parent, text="Feed (mm/min)").grid(row=1, column=0, sticky="w")
        ttk.Entry(parent, textvariable=self.jog_feed_var, width=10).grid(row=1, column=1, sticky="w")
        ttk.Button(parent, text="Y+", command=lambda: self._jog("Y", 1)).grid(row=2, column=1, sticky="ew", pady=(12, 4))
        ttk.Button(parent, text="X-", command=lambda: self._jog("X", -1)).grid(row=3, column=0, sticky="ew", padx=(0, 4), pady=4)
        ttk.Button(parent, text="X+", command=lambda: self._jog("X", 1)).grid(row=3, column=2, sticky="ew", padx=(4, 0), pady=4)
        ttk.Button(parent, text="Y-", command=lambda: self._jog("Y", -1)).grid(row=4, column=1, sticky="ew", pady=4)
        ttk.Button(parent, text="Z+", command=lambda: self._jog("Z", 1)).grid(row=5, column=0, sticky="ew", padx=(0, 4), pady=12)
        ttk.Button(parent, text="Z-", command=lambda: self._jog("Z", -1)).grid(row=5, column=2, sticky="ew", padx=(4, 0), pady=12)

    def _build_state_tab(self, parent: ttk.Frame) -> None:
        parent.columnconfigure(0, weight=1)
        ttk.Label(parent, textvariable=self.status_var, wraplength=420, justify="left").grid(row=0, column=0, sticky="ew")
        ttk.Label(parent, textvariable=self.pins_var, wraplength=420, justify="left").grid(row=1, column=0, sticky="ew", pady=(12, 0))

    def _build_settings_tab(self, parent: ttk.Frame) -> None:
        parent.columnconfigure(1, weight=1)
        ttk.Button(parent, text="Load $$", command=self._load_settings).grid(row=0, column=0, sticky="w")
        ttk.Button(parent, text="Apply Changed", command=self._apply_settings).grid(row=0, column=1, sticky="e")

        settings_grid = ttk.Frame(parent)
        settings_grid.grid(row=1, column=0, columnspan=2, sticky="nsew", pady=(8, 0))
        settings_grid.columnconfigure(1, weight=1)
        settings_grid.columnconfigure(4, weight=1)
        for idx, sid in enumerate(BENCH_SETTING_IDS):
            row = idx // 2
            col = (idx % 2) * 4
            label_text = f"${sid}  {SETTING_LABELS.get(sid, 'Setting')}"
            ttk.Label(settings_grid, text=label_text).grid(row=row, column=col, sticky="w", padx=(0, 6), pady=2)
            ttk.Entry(settings_grid, textvariable=self.setting_vars[sid], width=12).grid(row=row, column=col + 1, sticky="w", pady=2)
            if sid in MASK_SETTING_IDS:
                mask_frame = ttk.Frame(settings_grid)
                mask_frame.grid(row=row, column=col + 2, sticky="w", padx=(6, 12))
                for bit, axis in enumerate(("X", "Y", "Z")):
                    ttk.Checkbutton(
                        mask_frame,
                        text=axis,
                        variable=self.mask_vars[sid][bit],
                        command=lambda setting_id=sid: self._update_mask_entry(setting_id),
                    ).pack(side="left")

    def _log(self, text: str) -> None:
        self.log_widget.configure(state="normal")
        self.log_widget.insert("end", text.rstrip() + "\n")
        self.log_widget.see("end")
        self.log_widget.configure(state="disabled")

    def _send_command(self, command: str, settle: float = 0.7) -> str:
        assert self.runner.serial_session is not None
        response = self.runner.serial_session.send_line(command, settle=settle)
        self._log(f"> {command}")
        self._log(response or "<no response>")
        return response

    def _send_entry_command(self) -> None:
        command = self.command_var.get().strip()
        if not command:
            return
        self.command_var.set("")
        self._send_command(command)

    def _soft_reset(self) -> None:
        assert self.runner.serial_session is not None
        response = self.runner.serial_session.soft_reset(settle=1.0)
        self._log("> Ctrl-X")
        self._log(response or "<no response>")

    def _jog(self, axis: str, direction: int) -> None:
        distance = self.jog_distance_var.get().strip() or "1.0"
        feed = self.jog_feed_var.get().strip() or "100.0"
        sign = "" if direction > 0 else "-"
        self._send_command(f"$J=G21G91{axis}{sign}{distance}F{feed}", settle=0.6)

    def _refresh_status(self) -> None:
        response = self._send_command("?", settle=0.4)
        self.status_var.set(response or "status: <no response>")

    def _refresh_pins(self) -> None:
        response = self._send_command("$pins", settle=0.7)
        self.pins_var.set(response or "pins: <no response>")

    def _schedule_poll(self) -> None:
        try:
            if self.runner.serial_session is not None:
                response = self.runner.serial_session.send_line("?", settle=0.25)
                if response:
                    self.status_var.set(response)
        except Exception:
            pass
        self.root.after(1000, self._schedule_poll)

    def _load_settings(self) -> None:
        assert self.runner.serial_session is not None
        response = self.runner.serial_session.send_line("$$", settle=1.0)
        self._log("> $$")
        self._log(f"Loaded {len(parse_settings_dump(response))} settings from controller.")
        settings = parse_settings_dump(response)
        self.original_settings = {sid: settings.get(sid, "") for sid in BENCH_SETTING_IDS}
        for sid in BENCH_SETTING_IDS:
            self.setting_vars[sid].set(settings.get(sid, ""))
            if sid in MASK_SETTING_IDS:
                self._update_mask_checks(sid)

    def _apply_settings(self) -> None:
        reset_required = False
        for sid in BENCH_SETTING_IDS:
            value = self.setting_vars[sid].get().strip()
            if value and self.original_settings.get(sid) != value:
                self._send_command(f"${sid}={value}", settle=0.6)
                self.original_settings[sid] = value
                if sid == 395:
                    reset_required = True

        if reset_required:
            self._log("Default spindle selection changed; sending Ctrl-X to apply it.")
            self._soft_reset()

    def _update_mask_entry(self, setting_id: int) -> None:
        value = 0
        for bit, var in enumerate(self.mask_vars[setting_id]):
            if var.get():
                value |= 1 << bit
        self.setting_vars[setting_id].set(str(value))

    def _update_mask_checks(self, setting_id: int) -> None:
        try:
            value = int(float(self.setting_vars[setting_id].get().strip() or "0"))
        except ValueError:
            value = 0
        for bit, var in enumerate(self.mask_vars[setting_id]):
            var.set(1 if value & (1 << bit) else 0)

    def _show_step(self) -> None:
        if self.step_index >= len(self.steps):
            self.step_title_label.config(text="Bench Steps Complete")
            self.step_body_label.config(text="All GUI-driven bench steps are complete. Write the log and close when ready.")
            return

        title, body = self.steps[self.step_index]
        self.step_title_label.config(text=title)
        self.step_body_label.config(text=body)
        self.current_step_notes.set("")
        self._log(f"Step: {title}")
        self._log(body)

    def _complete_step(self, result: str) -> None:
        if self.step_index >= len(self.steps):
            return
        title, _body = self.steps[self.step_index]
        notes = self.current_step_notes.get().strip()
        self.runner.steps.append(StepLog(title, result, notes))
        self._log(f"Recorded result for {title}: {result}" + (f" ({notes})" if notes else ""))
        self.step_index += 1
        self._show_step()

    def _finish(self) -> None:
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


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
        self.firmware_tree_dirty = is_firmware_tree_dirty(self.git_state.root)
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
            "Firmware commit / tree state": self.defaults["Firmware commit / tree state"],
            "Firmware tree dirty": "yes" if self.firmware_tree_dirty else "no",
            "Build target": self.defaults["Build target"],
            "Build flags": self.defaults["Build flags"],
            "Flash method": self.defaults["Flash method"],
            "Serial console path": self.defaults["Serial console path"],
            "Detected serial port": self.defaults["Detected serial port"],
            "Board wiring notes": prompt("Board wiring notes", self.defaults["Board wiring notes"]),
            "PSU voltage": prompt("PSU voltage", self.defaults["PSU voltage"]),
            "Motors connected": prompt_yes_no("Motors connected", self.defaults["Motors connected"]),
            "Limit switches connected": prompt_yes_no("Limit switches connected", self.defaults["Limit switches connected"]),
            "Probe connected": prompt_yes_no("Probe connected", self.defaults["Probe connected"]),
            "Spindle connected": prompt_yes_no("Spindle connected", self.defaults["Spindle connected"]),
            "Laser connected": prompt_yes_no("Laser connected", self.defaults["Laser connected"]),
            "SD card inserted": prompt_yes_no("SD card inserted", self.defaults["SD card inserted"]),
            "Detected firmware artifacts": self.defaults["Detected firmware artifacts"],
            "Notes": prompt("Session notes", self.defaults["Notes"]),
        }

    def open_serial(self) -> None:
        port = self.args.port or detect_serial_port_candidate()
        if port is None:
            port = prompt("Serial port", "/dev/ttyUSB0")
        baud = self.args.baud
        print(f"\nOpening serial port {port} @ {baud}...")
        self.serial_session = SerialSession(port, baud)
        self.session_info["Detected serial port"] = port
        banner = self.serial_session.soft_reset(settle=1.2)
        self.steps.append(StepLog("Startup banner capture after Ctrl-X", "INFO", commands=["Ctrl-X"], responses=[banner]))
        if banner:
            print("\nStartup banner:")
            print(banner)
        else:
            print("\nNo startup banner was captured after Ctrl-X.")

    def reconnect_serial(self) -> None:
        port = self.session_info.get("Detected serial port") or self.args.port or detect_serial_port_candidate()
        baud = self.args.baud

        if self.serial_session is not None:
            self.serial_session.close()
            self.serial_session = None

        print("Disconnect the board power now if needed, then reconnect it before continuing.")
        input("Press Enter after the board is powered back up and the serial device has reappeared.")

        candidate = detect_serial_port_candidate()
        if self.args.port:
            port = self.args.port
        elif candidate is not None:
            port = candidate
        elif port is None:
            port = prompt("Serial port", "/dev/ttyUSB0")

        print(f"Reopening serial port {port} @ {baud}...")
        self.serial_session = SerialSession(port, baud)
        self.session_info["Detected serial port"] = port
        banner = self.serial_session.read_available(duration=1.2)
        self.steps.append(
            StepLog("Post power-cycle banner capture", "INFO", responses=[banner])
        )

    def handoff_serial(self, prompt_text: str) -> None:
        if self.serial_session is not None:
            self.serial_session.close()
            self.serial_session = None

        print("Serial port released.")
        print(prompt_text)
        input("Press Enter when you are done and want the runner to reconnect.")

        port = self.args.port or detect_serial_port_candidate() or self.session_info.get("Detected serial port")
        if port is None:
            port = prompt("Serial port", "/dev/ttyUSB0")

        print(f"Reopening serial port {port} @ {self.args.baud}...")
        self.serial_session = SerialSession(port, self.args.baud)
        self.session_info["Detected serial port"] = port
        self.serial_session.read_available(duration=0.5)

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
        dirty_tree = self.firmware_tree_dirty

        if dirty_tree and not self.args.allow_dirty_tree:
            print("\nCurrent HC32F460 firmware tree is dirty.")
            print(f"Repo state: {self.git_state.describe()}")
            print(f"Installed firmware: {installed}")
            print("Firmware-relevant files changed since that build. Rebuild and reflash, or rerun with --allow-dirty-tree.")
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
        self.launch_bench_gui()

    def launch_bench_gui(self) -> None:
        if tk is None or ttk is None or ScrolledText is None:
            print("\nTkinter is not available; falling back to CLI prompts for the remaining manual steps.")
            self.step_motion_basics()
            self.step_limits_homing()
            self.step_probe()
            self.step_motion_stability()
            self.step_primary_spindle()
            self.step_secondary_spindle_laser()
            self.step_laser_specific()
            self.step_optional_controls()
            self.step_alt_uart()
            return
        gui = BenchControlGui(self)
        gui.run()

    def serial_command_step(self, title: str, commands: List[str], settle: float = 0.7) -> None:
        assert self.serial_session is not None
        responses: List[str] = []
        print(f"\n{title}")
        for command in commands:
            print(f"  -> {command}")
            response = self.serial_session.send_line(command, settle=settle)
            responses.append(response)
            print(textwrap.indent(response or "<no response>", prefix="     "))
        result = choose_result()
        notes = prompt("Notes", "")
        self.steps.append(StepLog(title, result, notes, commands, responses))

    def auto_serial_probe_step(self, title: str, commands: List[str], settle: float = 0.7) -> None:
        assert self.serial_session is not None
        responses: List[str] = []
        ok = True

        print(f"\n{title}")
        for command in commands:
            print(f"  -> {command}")
            response = self.serial_session.send_line(command, settle=settle)
            responses.append(response)
            print(textwrap.indent(response or "<no response>", prefix="     "))
            if not response.strip():
                ok = False

        result = "PASS" if ok else "FAIL"
        default_notes = "" if ok else "One or more commands returned no response."
        print(f"Auto result: {result}")
        if ok:
            print("Summary: serial ok, firmware responded, status responded, pins enumerated.")
        notes = prompt("Notes", default_notes)
        self.steps.append(StepLog(title, result, notes, commands, responses))

    def interactive_step(self, title: str, guidance: str) -> None:
        print(f"\n{title}")
        print(textwrap.indent(wrap_block(guidance), prefix="  "))
        result = choose_result()
        notes = prompt("Notes", "")
        self.steps.append(StepLog(title, result, notes))

    def step_boot_console(self) -> None:
        self.auto_serial_probe_step(
            "Boot And Console",
            ["$I", "$$", "?", "$pins"],
            settle=0.9,
        )

    def step_eeprom_persistence(self) -> None:
        assert self.serial_session is not None
        if not confirm("\nRun EEPROM/settings persistence check now?", False):
            self.steps.append(StepLog("EEPROM / Settings Persistence", "SKIP", "Skipped by user."))
            return

        setting_ref = prompt("Setting to test", "$10").strip()
        if not setting_ref.startswith("$") or not setting_ref[1:].isdigit():
            self.steps.append(StepLog("EEPROM / Settings Persistence", "FAIL", f"Unsupported setting reference: {setting_ref}"))
            return

        setting_id = int(setting_ref[1:])
        verify = "$$"
        desired_value = prompt("Temporary value to write", "3").strip()

        responses = []
        commands = [verify]
        baseline = self.serial_session.send_line(verify, settle=0.8)
        responses.append(baseline)

        original_value = parse_setting_value(baseline, setting_id)
        if original_value is None:
            self.steps.append(
                StepLog(
                    "EEPROM / Settings Persistence",
                    "FAIL",
                    f"Could not determine original value for {setting_ref} from $$ output.",
                    commands,
                    responses,
                )
            )
            return

        if desired_value == original_value:
            desired_value = "1" if original_value != "1" else "3"

        setting = f"${setting_id}={desired_value}"
        restore = f"${setting_id}={original_value}"

        print(f"Original {setting_ref} value: {original_value}")
        print(f"Writing temporary value: {desired_value}")

        commands.extend([setting, verify])
        responses.append(self.serial_session.send_line(setting, settle=0.8))
        after_write = self.serial_session.send_line(verify, settle=0.8)
        responses.append(after_write)

        print("Now soft reset the controller.")
        responses.append(self.serial_session.soft_reset(settle=1.0))
        print("Closing the serial port before power-cycle verification.")
        self.reconnect_serial()
        assert self.serial_session is not None
        after_power_cycle = self.serial_session.send_line(verify, settle=0.8)
        responses.append(after_power_cycle)

        print(f"Restoring {setting_ref} to its original value ({original_value}).")
        commands.extend(["Ctrl-X", "power-cycle", verify, restore, verify])
        responses.append(self.serial_session.send_line(restore, settle=0.8))
        after_restore = self.serial_session.send_line(verify, settle=0.8)
        responses.append(after_restore)

        result = choose_result()
        notes = prompt("Notes", "")
        self.steps.append(
            StepLog("EEPROM / Settings Persistence", result, notes, commands, responses)
        )

    def step_idle_inputs(self) -> None:
        self.serial_command_step("Idle Input Sanity", ["$pins", "?"], settle=0.8)

    def step_motion_basics(self) -> None:
        guidance = (
            "Jog X, Y, and Z in both directions at low speed. Verify direction, smoothness, and that the "
            "mechanics are clear. Record any inversion, roughness, stalls, chatter, or timer issues."
        )
        if confirm("\nRelease the serial port so you can use another sender for motion tests?", True):
            self.handoff_serial(
                "Use your normal sender now for jogging and motion checks."
            )
        self.interactive_step("Motion Basics", guidance)

    def step_limits_homing(self) -> None:
        guidance = (
            "Manually trigger X, Y, and Z limits and observe $pins. Then test homing and, when safe, "
            "hard-limit trips during slow motion. Watch for wrong polarity, false triggers, or incorrect "
            "axis mapping."
        )
        if confirm("\nRelease the serial port so you can use another sender for homing/limit tests?", True):
            self.handoff_serial(
                "Use your normal sender now for homing and hard-limit tests."
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
        if confirm("\nRelease the serial port so you can use another sender for probe cycles?", True):
            self.handoff_serial(
                "Use your normal sender now for probe-cycle testing."
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
    port = port_arg or detect_serial_port_candidate()
    if port is None:
        port = prompt("Serial port", "/dev/ttyUSB0")
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
