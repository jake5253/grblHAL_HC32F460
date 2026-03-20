import os

from SCons.Script import DefaultEnvironment


env = DefaultEnvironment()

project_dir = env["PROJECT_DIR"]
build_dir = env.subst("$BUILD_DIR")

core_dir = os.path.join(project_dir, "grbl")
sdk_src_dir = os.path.join(project_dir, "SDK", "drivers", "library", "src")

sdk_sources = [
    "hc32f46x_clk.c",
    "hc32f46x_efm.c",
    "hc32f46x_exint_nmi_swi.c",
    "hc32f46x_gpio.c",
    "hc32f46x_pwc.c",
    "hc32f46x_timera.c",
    "hc32f46x_usart.c",
]

env.Append(
    ASFLAGS=["-mcpu=cortex-m4", "-mthumb"],
    CCFLAGS=["-mcpu=cortex-m4", "-mthumb"],
    LINKFLAGS=["-mcpu=cortex-m4", "-mthumb", "-nostartfiles", "-Wl,--gc-sections"],
    LIBS=["m"],
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
