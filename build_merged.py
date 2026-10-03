# This script adds a "build_merged" target, used like
# "pio run -e wifi_s3 -t build_merged", that creates a combined
# image with bootloader, partition table, firmware, and filesystem at the
# correct offsets for the selected flash layout. Build the firmware first
# with "pio run"; this target also builds the filesystem.
import subprocess
import sys

Import("env")


def build_filesystem(source, target, env):
    # Use the same PlatformIO installation, including when pio is not on PATH.
    return subprocess.call([sys.executable, "-m", "platformio", "run", "-e", env["PIOENV"], "-t", "buildfs"])


def merge_flash(source, target, env):
    uploader = env.subst("$UPLOADER").strip('"')
    # Older platforms use esptool.py; pioarduino uses an executable launcher.
    cmd = [env.subst("$PYTHONEXE"), uploader] if uploader.endswith(".py") else [uploader]
    cmd += [
        "--chip", env.subst("$BOARD_MCU"), "merge_bin",
        "--output", env.subst("$BUILD_DIR/merged-flash.bin"),
        "--flash_mode", "dio",
        "--flash_size", env.BoardConfig().get("upload.flash_size", "detect"),
    ]
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        cmd += [str(offset), env.subst(path)]
    cmd += [
        "0x10000", env.subst("$BUILD_DIR/firmware.bin"),
        env.GetProjectOption("custom_filesystem_start"), env.subst("$BUILD_DIR/littlefs.bin"),
    ]
    return subprocess.call(cmd)


env.AddCustomTarget(
    name="build_merged",
    dependencies=["$BUILD_DIR/bootloader.bin", "$BUILD_DIR/firmware.bin"],
    actions=[build_filesystem, merge_flash],
    title="Build Merged",
    description="Build combined image with program and filesystem"
)
