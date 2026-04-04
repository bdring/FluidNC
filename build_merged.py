# This script adds a "build_merged" target, used like
# "pio run -e wifi_s3 -t build_merged", that creates a combined
# image with bootloader, partition table, firmware, and filesystem at the
# correct offsets for an 8MB flash.  The smaller images must be built first
# with "pio run" and "pio run -t buildfs".
Import("env")

flash_size = env.BoardConfig().get("upload.flash_size", "detect")

cmd = '$PYTHONEXE $UPLOADER --chip $BOARD_MCU merge_bin --output $BUILD_DIR/merged-flash.bin --flash_mode dio --flash_size ' + flash_size + " "

for image in env.get("FLASH_EXTRA_IMAGES", []):
    cmd += image[0] + " " + env.subst(image[1]) + " "

filesystem_start = env.GetProjectOption("custom_filesystem_start", "Missing_custom_filesystem_start_variable")

cmd += " 0x10000 $BUILD_DIR/firmware.bin " + filesystem_start + " $BUILD_DIR/littlefs.bin"

env.AddCustomTarget(
    name="build_merged",
    dependencies=["$BUILD_DIR/bootloader.bin", "$BUILD_DIR/firmware.bin"],
    actions=["pio run -e $PIOENV -t buildfs", cmd],
    title="Build Merged",
    description="Build combined image with program and filesystem"
)
