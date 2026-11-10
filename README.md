# WiiMoteDLL_Libusbk

Reusable USB Bluetooth host for Nintendo Wii Remotes on Windows, with a versioned C ABI and a standalone hardware diagnostic.

## Components

- **`wii_bt_passthrough.dll`**: optional two-player backend for applications. It performs USB HCI adapter selection, Wii Remote discovery, baseband connection, legacy PIN authentication, link-key restore/save, encryption, L2CAP HID channels, HIDP input and IR initialization. The public API is in [`include/wii_bt_api.h`](include/wii_bt_api.h).
- **`wii_discovery.exe`**: the independent low-level diagnostic used to validate real adapter/remote HCI, pairing, encryption, L2CAP and HIDP behavior. The SYNC flow was verified with a physical Wii Remote and CSR8510 A10 (`0A12:0001`) through libusbK on Windows. The program is retained as a hardware diagnostic, not used as the DLL's implementation.
- **libusb**: USB transport dependency; it remains LGPL-2.1-or-later and is not relicensed by this repository.

The reusable runtime was extracted from HOTD2RE's Windows USB backend so the game and other clients can consume the same independently built DLL instead of compiling a private Bluetooth stack into each game. Its source retains the upstream HOTD2RE MIT license; see [Third-party notices](THIRD_PARTY_NOTICES.md) and [LICENSE-HOTD2RE](LICENSE-HOTD2RE). The independent diagnostic and other original project code are under 0BSD.

## Adapter selection

The library does not assume CSR8510 or any particular vendor ID.

- Automatic: pass `-1, -1`; the runtime enumerates standard USB Bluetooth HCI devices and tries to claim a compatible interface.
- Explicit: pass a VID/PID pair to constrain selection. It does not silently fall back to another adapter.
- The game/application should keep native Windows Bluetooth HID as its default path and load this DLL only when the user explicitly enables USB passthrough.

The HCI interface must be accessible through libusb. On Windows, use a compatible WinUSB/libusbK driver for the chosen adapter. An adapter owned exclusively by the Windows Bluetooth stack may not be claimable through libusb.

## Build

Install the libusb development package so CMake can locate `libusb.h` and `libusb-1.0`.

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The build produces `wii_bt_passthrough.dll` and, by default, `wii_discovery.exe`. Disable the diagnostic target with `-DWII_BT_BUILD_DIAGNOSTIC=OFF` when embedding the project as a dependency. The process must also be able to load `libusb-1.0.dll`.

The library supports the architecture selected by the toolchain (Win32 or x64); the consumer and DLL must have matching architectures.

## API

API version: **3**.

`WiiBt_Create(vid, pid)` uses generic per-user storage under `%LOCALAPPDATA%\\WiiBluetoothHost`. `WiiBt_CreateWithStorage(vid, pid, directory)` allows the application to supply its own UTF-16 storage directory; the library stores `wii_link_keys.txt` there. This keeps the library independent from any game's configuration or profile paths.

The API supports create/destroy, enable/disable, scan, disconnect, player swap, and polling a stable fixed-size snapshot. No C++ STL types cross the DLL boundary.

## Diagnostic commands

```text
wii_discovery.exe
wii_discovery.exe 0x0A12 0x0001
wii_discovery.exe --connect 40:D2:8A:07:EB:36 sync
wii_discovery.exe --connect 40:D2:8A:07:EB:36 sync 0x0A12 0x0001
wii_discovery.exe --connect 40:D2:8A:07:EB:36 1+2 0x0A12 0x0001
```

The diagnostic's SYNC test completed HCI discovery, baseband connection, legacy PIN authentication, link encryption, L2CAP control/interrupt setup and HIDP input reception with a physical Wii Remote. This hardware result applies to the tested CSR8510/libusbK combination; it is not a blanket compatibility claim for every adapter.

## License

Original diagnostic and project code: **0BSD** (see [LICENSE](LICENSE)). The extracted HOTD2RE runtime file is **MIT-licensed**, with its license retained separately. libusb and other dependencies retain their own licenses.
