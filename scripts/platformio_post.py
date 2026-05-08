import os
import shutil

from SCons.Script import DefaultEnvironment


env = DefaultEnvironment()

project_dir = env["PROJECT_DIR"]
artifact_dir = os.path.join(project_dir, "build")


def _copy_artifacts(source, target, env):
    os.makedirs(artifact_dir, exist_ok=True)

    elf_path = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    bin_path = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    map_path = env.subst("$BUILD_DIR/${PROGNAME}.map")

    for src, dst in (
        (elf_path, os.path.join(artifact_dir, "firmware.elf")),
        (bin_path, os.path.join(artifact_dir, "firmware.bin")),
        (map_path, os.path.join(artifact_dir, "firmware.map")),
    ):
        if os.path.exists(src):
            shutil.copy2(src, dst)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _copy_artifacts)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _copy_artifacts)
