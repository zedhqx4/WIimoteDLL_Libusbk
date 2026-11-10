# WiiMoteDLL_Libusbk

Independent USB Bluetooth host for Nintendo Wii Remotes, intended to be consumed by multiple game projects.

## Goals

- Own a dedicated USB Bluetooth HCI adapter through libusb/libusbK.
- Discover and connect to Wii Remotes using both 1+2 and SYNC-button procedures.
- Implement Bluetooth Classic HCI, L2CAP, HIDP, and Wii Remote report handling.
- Keep the Windows Bluetooth stack and unrelated radios untouched.
- Expose a stable API to game integrations and provide standalone diagnostic utilities.

## Current hardware baseline

The CSR8510 A10 adapter (USB VID:PID `0A12:0001`) has been tested with libusbK on Windows. USB interface claim/release and HCI command/event exchange have been verified using separate probes in HOTD2RE.

Verified hardware operations from the HOTD2RE probes:

- USB HCI interface claim/release
- HCI Reset (`0x0C03`)
- Read Local Version Information (`0x1001`)
- Read BD_ADDR (`0x1009`)

## Current implementation

The `feature/hci-discovery-pairing` branch contains the `wii_discovery` diagnostic executable:

- Default mode: resets the adapter and performs General Inquiry (GIAC), printing discovered addresses and Class of Device values.
- Connection mode: targets a supplied Bluetooth address, reads the host adapter address, issues HCI Create Connection, requests authentication, rejects an unknown stored link key, and automatically answers a legacy PIN request for either SYNC or 1+2 mode.

Connection-mode syntax:

```text
wii_discovery.exe --connect 40:D2:8A:07:EB:36 sync
wii_discovery.exe --connect 40:D2:8A:07:EB:36 1+2
```

The optional adapter VID/PID can be supplied after the pairing mode:

```text
wii_discovery.exe --connect 40:D2:8A:07:EB:36 sync 0x0A12 0x0001
```

The legacy PIN is handled internally as six raw Bluetooth address bytes; there is no user-entered PIN. SYNC uses the host adapter address (bonding), while 1+2 uses the Wii Remote address (temporary pairing). The diagnostic currently does not persist link keys. The latest development step adds USB ACL bulk transport and attempts the two Wii Remote HID L2CAP PSMs (0x0011 control and 0x0013 interrupt). These channels still need L2CAP configuration and HIDP setup before input reports can flow.

**Important:** CI compilation is not hardware verification. The new connection/authentication path must be tested against the CSR8510 and a real Wii Remote before it is considered working.

## Development order

1. **Discovery and baseband authentication:** HCI inquiry, connection, authentication, and link-key handling for both 1+2 and SYNC.
2. **L2CAP:** establish and configure the HID control and interrupt channels; validate ACL packet handling against the real remote.
3. **HIDP:** configure the remote and receive input reports.
4. **Reusable host/API:** separate the Bluetooth host from game-specific input adapters.
5. **Pairing utility:** provide a standalone tool to inspect the adapter and manage remote connections.

## Licensing

The repository is intended to host a separately distributed component. Any Dolphin-derived source must retain its applicable GPL-2.0-or-later notices and corresponding source-distribution obligations. Game projects communicate with the host through a documented interface; licensing implications of the concrete integration model must be reviewed before distribution.
