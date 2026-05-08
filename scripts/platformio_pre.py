import os
import subprocess

from SCons.Script import DefaultEnvironment


env = DefaultEnvironment()

project_dir = env["PROJECT_DIR"]
build_dir = env.subst("$BUILD_DIR")

core_dir = os.path.join(project_dir, "grbl")
sdk_src_dir = os.path.join(project_dir, "SDK", "drivers", "library", "src")
fatfs_dir = os.path.join(project_dir, "fatfs")
sdcard_dir = os.path.join(project_dir, "sdcard")
keypad_dir = os.path.join(project_dir, "keypad")
fans_dir = os.path.join(project_dir, "fans")
odometer_dir = os.path.join(project_dir, "odometer")
addons_dir = os.path.join(project_dir, "addons")
spindle_select_dir = os.path.join(addons_dir, "spindle_select")

sdk_sources = [
    "hc32f46x_adc.c",
    "hc32f46x_clk.c",
    "hc32f46x_efm.c",
    "hc32f46x_exint_nmi_swi.c",
    "hc32f46x_gpio.c",
    "hc32f46x_pwc.c",
    "hc32f46x_timera.c",
    "hc32f46x_usart.c",
]


def run_command(args, cwd):
    try:
        result = subprocess.run(
            args,
            cwd=cwd,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.SubprocessError, FileNotFoundError):
        return None

    return result.stdout.strip()


def stringify_macro(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def detect_build_info(project_dir):
    branch = run_command(["git", "rev-parse", "--abbrev-ref", "HEAD"], project_dir) or "unknown"
    commit = run_command(["git", "rev-parse", "--short", "HEAD"], project_dir) or "unknown"
    status = run_command(["git", "status", "--short"], project_dir) or ""
    dirty = "dirty" if status else "clean"
    base = "FFP0173_Aquila_Main_Board_V1.0.2"

    return f"{base}|git={commit}|branch={branch}|tree={dirty}"


generated_build_info = detect_build_info(project_dir)
generated_header = os.path.join(build_dir, "generated_build_info.h")

os.makedirs(build_dir, exist_ok=True)

with open(generated_header, "w", encoding="ascii") as header:
    header.write("#pragma once\n")
    header.write(f"#define BUILD_INFO {stringify_macro(generated_build_info)}\n")

env.Append(
    ASFLAGS=["-mcpu=cortex-m4", "-mthumb"],
    CCFLAGS=["-mcpu=cortex-m4", "-mthumb", "-include", generated_header],
    LINKFLAGS=["-mcpu=cortex-m4", "-mthumb", "-nostartfiles", "-Wl,--gc-sections"],
    LIBS=["m"],
    CPPPATH=[project_dir, fatfs_dir, os.path.join(fatfs_dir, "port"), sdcard_dir, keypad_dir],
    CPPDEFINES=["HC32_PLATFORM", "STM32_PLATFORM", "_USE_IOCTL=1", "_USE_WRITE=1"],
)

env.BuildSources(
    os.path.join(build_dir, "grblcore"),
    core_dir,
    src_filter=["+<*.c>", "+<kinematics/*.c>"],
)

env.BuildSources(
    os.path.join(build_dir, "sdk"),
    sdk_src_dir,
    src_filter=["+<%s>" % src for src in sdk_sources],
)

env.BuildSources(
    os.path.join(build_dir, "fatfs"),
    fatfs_dir,
    src_filter=["+<ff.c>", "+<ffsystem.c>", "+<ffunicode.c>"],
)

env.BuildSources(
    os.path.join(build_dir, "sdcard"),
    sdcard_dir,
    src_filter=["+<fs_fatfs.c>", "+<fs_stream.c>", "+<macros.c>", "+<sdcard.c>", "+<ymodem.c>"],
)

env.BuildSources(
    os.path.join(build_dir, "addons", "spindle_select"),
    spindle_select_dir,
    src_filter=["+<select.c>"],
)

env.BuildSources(
    os.path.join(build_dir, "keypad"),
    keypad_dir,
    src_filter=["+<keypad.c>", "+<macros.c>"],
)

env.BuildSources(
    os.path.join(build_dir, "fans"),
    fans_dir,
    src_filter=["+<fans.c>"],
)

env.BuildSources(
    os.path.join(build_dir, "odometer"),
    odometer_dir,
    src_filter=["+<odometer.c>"],
)
