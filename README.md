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

Verified hardware operations from the HOTD2RE probes:

- USB HCI interface claim/release
- HCI Reset (`0x0C03`)
- Read Local Version Information (`0x1001`)
- Read BD_ADDR (`0x1009`)

## Current implementation

The `feature/hci-discovery-pairing` branch contains an initial `wii_discovery` executable. It claims the USB HCI interface, resets the controller, starts a General Inquiry (GIAC), and prints discovered Bluetooth addresses and Class of Device values.

This is the first active discovery step, **not pairing yet**. The next increment will identify Wii Remotes, create the baseband connection, and implement the two Wii Remote PIN procedures (1+2 and SYNC).

The new discovery executable has been committed but has not yet been compiled or tested on the user's CSR8510. Do not treat it as hardware-verified.

## Development order

1. **Discovery and pairing:** HCI inquiry, Wii Remote identification, connection, authentication, and link-key handling for both 1+2 and SYNC.
2. **L2CAP:** establish the HID control and interrupt channels.
3. **HIDP:** configure the remote and receive input reports.
4. **Reusable host/API:** separate the Bluetooth host from game-specific input adapters.
5. **Pairing/diagnostic utility:** provide a standalone tool to inspect the adapter and manage remote connections.

## Licensing

The repository is intended to host a separately distributed component. Any Dolphin-derived source must retain its applicable GPL-2.0-or-later notices and corresponding source-distribution obligations. Game projects communicate with the host through a documented interface; licensing implications of the concrete integration model must be reviewed before distribution.
