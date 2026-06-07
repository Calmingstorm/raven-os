# RavenOS USB boot image

RavenOS builds a GRUB multiboot ISO and an explicit USB-writeable hybrid ISO artifact.

## Build

```sh
./tools/build-usb-iso.sh
```

Artifacts:

- `build/ravenos-usb.iso` — hybrid ISO intended for direct USB writing.
- `build/ravenos-usb.iso.sha256` — checksum for the exact image.

## Validate without real hardware

```sh
./tools/usb-iso-qemu-test.sh
```

This boots `build/ravenos-usb.iso` as a raw IDE disk in QEMU instead of as a CD-ROM. If the image is not USB/hard-disk bootable, this test should fail before the RavenOS banner and `raven>` prompt appear.

## Write to removable media

Use a raw image writer such as balenaEtcher, Raspberry Pi Imager, GNOME Disks, or Rufus raw/image mode. Select `build/ravenos-usb.iso` as the image and the whole USB stick as the target. This overwrites the selected device, so choose carefully. Midgard has enough casualties already.

## Current limits

This is a bootable development OS image, not an installed production OS. It boots to the RavenOS shell and supports the current in-kernel RavenFS/dev-disk features when a writable dev disk is attached separately. The USB ISO itself is treated as read-only boot media.
