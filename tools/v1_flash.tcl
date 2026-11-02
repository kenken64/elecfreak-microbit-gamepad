# OpenOCD flashing script for env:microbit_v1 (nRF51822).
#
# Programs and boots the gamepad firmware (S130 SoftDevice + app). Expects
# FIRMWARE to be set to the merged .hex.
#
# A micro:bit that last ran a MakeCode / micro:bit-DAL program still has a
# Nordic DFU bootloader at 0x3C000 and its address in UICR.NRFFW[0]. The
# SoftDevice's boot stub (MBR) would keep jumping into that stale bootloader,
# so the gamepad never starts. On the nRF51 the UICR can only be cleared by a
# full mass erase, so in that case (only) the whole chip is erased first.
# Otherwise the bond page is kept, so Windows stays paired across re-flashes.

init
reset halt

set bootloader_addr [lindex [read_memory 0x10001014 32 1] 0]
if {$bootloader_addr != 0xffffffff} {
	echo [format "Found old bootloader address 0x%08x in UICR -> mass erase" $bootloader_addr]
	nrf5 mass_erase
}

program $FIRMWARE verify
reset run
shutdown
