Quickboot
=========

Misc. stuff from my master's thesis, available at
http://liu.diva-portal.org/smash/get/diva2:473038/FULLTEXT01 .

dhcpevent    : Receives DHCP events from udhcpc (bundled with BusyBox) and
               configures network interfaces.

diskgrub     : Grub Legacy configuration for booting from disk.

initscript   : Small initialization script run by start.c .

minkernel.py : Generates a kernel with a minimal feature set using Kconfiglib
               and automatic testing in QEMU.

shutdown.c   : Tiny shutdown utility.

start.c      : Tiny init process.

usbboot.c    : Run from an initramfs to boot from a USB device.

usbgrub      : Grub Legacy configuration for booting from a USB device.
