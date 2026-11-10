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

The legacy PIN is handled internally as six raw Bluetooth address bytes; there is no user-entered PIN. SYNC uses the host adapter address (bonding), while 1+2 uses the Wii Remote address (temporary pairing). The diagnostic currently does not persist link keys.

**Hardware-verified status:** the SYNC-button path has been tested with a real Wii Remote and a CSR8510 A10 (0A12:0001) using libusbK on Windows. The test completed HCI discovery, baseband connection, legacy PIN authentication, link encryption, L2CAP control/interrupt setup and HIDP input reception; real core-button reports were received. This is a hardware result for that adapter and remote, not a claim that every USB Bluetooth adapter is compatible.

The current branch still builds **only the standalone wii_discovery.exe diagnostic**. It is not yet the reusable DLL/library. Link-key persistence, a public application-independent API, adapter enumeration/selection and integration tests remain to be implemented in this repository. The separate HOTD2RE integration currently contains a second implementation; the long-term plan is to extract and share the proven protocol core rather than maintain two independent stacks.

## Development order

1. **Extract a shared host core:** move the proven HCI/L2CAP/HIDP implementation into a library used by both the diagnostic CLI and game integrations.
2. **Adapter enumeration and selection:** expose all claimable standard USB Bluetooth HCI devices, allow explicit VID/PID selection, and keep automatic selection as an option.
3. **Reusable API and lifecycle:** provide documented C and/or C++ interfaces, caller-controlled link-key storage, asynchronous report delivery, and clean shutdown.
4. **Compatibility validation:** test more adapter chipsets, Windows driver configurations, Wii Remote pairing modes and reconnect behavior.
5. **Distribution:** provide CMake packages, x86/x64 Windows builds, a sample client and diagnostics.

## License and project independence

Original code in this repository is released under **0BSD** (see [LICENSE](LICENSE)). This permissive, OSI-approved license allows game projects and other applications to use, modify, redistribute, and incorporate the library with minimal restrictions, including in closed-source products.

The project takes inspiration from Dolphin's Bluetooth-adapter passthrough feature, but the reusable host implementation is maintained independently. We do not copy or relicense Dolphin source code under 0BSD. Any future code copied or adapted from Dolphin must remain clearly separated, retain its original GPL-2.0-or-later notices, and be reviewed before inclusion; it cannot be relicensed as 0BSD.

Third-party dependencies keep their own licenses. In particular, **libusb remains LGPL-2.1-or-later**; the 0BSD grant applies to this repository's original code, not to libusb or other bundled dependencies.
