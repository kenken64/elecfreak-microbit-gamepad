# PlatformIO pre-build hook for env:microbit_v1.
#
# The Arduino nRF5 core ships the S130 headers but not the SoftDevice binary.
# Point the build at our copy so firmware.hex = S130 SoftDevice + application
# (PlatformIO merges them with srec_cat).
#
# softdevice/s130_nrf51_2.0.1_softdevice.hex is taken unmodified from Nordic's
# nRF5 SDK 12.3.0 (MD5 f63d43ae56cada5d5aaa14271fc76681); see the licence
# agreement in the same folder.
import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

hex_path = os.path.join(env.subst("$PROJECT_DIR"), "softdevice",
                        "s130_nrf51_2.0.1_softdevice.hex")

if not os.path.isfile(hex_path):
    raise SystemExit("S130 SoftDevice not found: %s" % hex_path)

env.Replace(SOFTDEVICEHEX=hex_path)  # noqa: F821
