# PlatformIO post-script for env:microbit_v1: replace the stock OpenOCD
# "program" upload with tools/v1_flash.tcl (clears a stale MakeCode/DAL
# bootloader when present, see that file).
import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

openocd_dir = env.PioPlatform().get_package_dir("tool-openocd") or ""  # noqa: F821
flash_tcl = os.path.join(env.subst("$PROJECT_DIR"), "tools", "v1_flash.tcl")  # noqa: F821


def _tcl_path(path):
    return path.replace("\\", "/")


env.Replace(  # noqa: F821
    UPLOADER=os.path.join(openocd_dir, "bin", "openocd"),
    UPLOADERFLAGS=[
        "-s", _tcl_path(os.path.join(openocd_dir, "openocd", "scripts")),
        "-f", "interface/cmsis-dap.cfg",
        "-c", "transport select swd",
        "-f", "target/nrf51.cfg",
        "-c", "set FIRMWARE {$SOURCE}",
        "-f", _tcl_path(flash_tcl),
    ],
    UPLOADCMD='"$UPLOADER" $UPLOADERFLAGS',
)
