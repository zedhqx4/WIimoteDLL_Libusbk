# WiiMoteDLL_Libusbk

Independent USB Bluetooth host for Nintendo Wii Remotes, intended to be consumed by multiple game projects.

## Goals

- Own a dedicated USB Bluetooth HCI adapter through libusb/libusbK.
- Discover and pair Wii Remotes using both 1+2 and SYNC-button procedures.
- Implement Bluetooth Classic HCI, L2CAP, HIDP, and Wii Remote report handling.
- Keep the Windows Bluetooth stack and unrelated radios untouched.
- Expose a stable API to game integrations and provide a standalone diagnostic/pairing utility.

## Current hardware baseline

The CSR8510 A10 adapter (USB VID:PID `0A12:0001`) has been tested with libusbK on Windows. USB interface claim/release and HCI command/event exchange have been verified using separate probes in HOTD2RE.

Verified HCI operations so far:

- HCI Reset (`0x0C03`)
- Read Local Version Information (`0x1001`)
- Read BD_ADDR (`0x1009`)

These checks do not yet implement discovery, pairing, L2CAP, or HID.

## Development order

1. **Discovery and pairing:** HCI inquiry, Wii Remote identification, connection, authentication, and link-key handling for both 1+2 and SYNC.
2. **L2CAP:** establish the HID control and interrupt channels.
3. **HIDP:** configure the remote and receive input reports.
4. **Reusable host/API:** separate the Bluetooth host from game-specific input adapters.
5. **Pairing/diagnostic utility:** provide a standalone tool to inspect the adapter and manage remote connections.

## License

Original code in this repository is released under **0BSD** (see [LICENSE](LICENSE)). This is a permissive, OSI-approved license that allows use, modification, redistribution, and incorporation into other projects, including closed-source and commercial applications, without an attribution condition.

The implementation is inspired by Dolphin's Bluetooth passthrough feature, but this repository does not copy or relicense Dolphin source code. Any future Dolphin-derived code must retain its GPL-2.0-or-later license and notices and must be reviewed separately; it cannot be relicensed as 0BSD.

Third-party dependencies retain their own licenses. In particular, libusb is LGPL-2.1-or-later; the 0BSD license applies only to this repository's original code.

## Status

The repository was initially empty. Implementation has not started; the verified hardware baseline above comes from the separate HOTD2RE probe.
