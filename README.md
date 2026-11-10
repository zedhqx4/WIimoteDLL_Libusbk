# WiiMoteDLL_Libusbk

An application-independent USB Bluetooth host for Nintendo Wii Remotes. The project builds a reusable Windows DLL, a small diagnostic client that consumes the DLL, and a public C API suitable for game integrations.

## What it does

- Owns a USB Bluetooth HCI adapter through libusb, without using the Windows Bluetooth stack for that adapter.
- Discovers and connects to up to two Nintendo Wii Remotes.
- Supports the legacy SYNC-button and 1+2 pairing PIN procedures.
- Performs Bluetooth Classic HCI connection, authentication and link encryption.
- Opens and configures Wii Remote HID L2CAP Control (0x0011) and Interrupt (0x0013) channels.
- Configures HIDP input mode 0x33, LEDs, status and infrared reporting.
- Reports buttons, battery state and IR points through a versioned C ABI.
- Keeps game-specific action mapping, calibration policy and player controls in the consuming application.

The host owns its USB device, libusb context and worker thread. The public boundary uses fixed-width C structs and opaque handles; no C++ containers or allocator-owned objects cross the DLL boundary.

## Adapter compatibility and selection

The library is **not tied to a CSR8510 chipset or VID/PID**. It enumerates USB devices that expose the standard Bluetooth HCI interface:

- Interface class E0, subclass 01, protocol 01.
- Interrupt IN endpoint for HCI events.
- Bulk IN and Bulk OUT endpoints for ACL data.
- The interface must be accessible and claimable through libusb.

Windows requires a libusb-supported driver on the selected USB adapter, commonly WinUSB or libusbK. A radio appearing in Windows Bluetooth settings does not automatically make it usable through USB passthrough. Changing the driver with Zadig may remove that adapter from the normal Windows Bluetooth stack, so a dedicated USB adapter is recommended.

The CSR8510 A10 (0A12:0001) was the first physical test adapter. It is a test result, not a special case in the library.

The API accepts either automatic selection or a specific VID/PID pair:

- vid=-1, pid=-1: use the first claimable standard HCI adapter.
- A positive VID and PID: use only a matching adapter; do not silently fall back if it is missing or cannot be claimed.
- If multiple identical adapters share the same VID/PID, the current selector uses the first matching USB device. Enumeration exposes bus/address details for applications that need to present a list, but selecting between identical VID/PID devices by USB location is not yet exposed by WiiBt_Create.

## Public API

Include include/wii_bt_api.h. API version 3 is a C ABI and is usable from C, C++, or other languages with C FFI support.

The host instance runs its own worker thread. Applications call WiiBt_Poll from their input/update thread to retrieve a snapshot.

~~~c
#include "wii_bt_api.h"

int32_t count = WiiBt_EnumerateAdapters(NULL, 0);
/* Allocate count WiiBtAdapterInfo records and enumerate again. */

void* host = WiiBt_Create(
    -1, -1,                         /* automatic adapter selection */
    WII_BT_PAIRING_SYNC,            /* or WII_BT_PAIRING_ONE_PLUS_TWO */
    NULL);                          /* use the library's default key file */

WiiBt_Enable(host, 1, 0);
WiiBt_Scan(host);

/* During the application's update loop: */
WiiBtSnapshot snapshot = {0};
snapshot.struct_size = sizeof(snapshot);
WiiBt_Poll(host, &snapshot);

/* On shutdown: */
WiiBt_Enable(host, 0, 0);
WiiBt_Destroy(host);
~~~

WiiBt_EnumerateAdapters returns the number of claimable adapters. Pass output=NULL and capacity=0 to query the count, then pass a buffer to receive VID/PID, bus/address, interface number, manufacturer and product strings.

### Link-key storage

The key_file_utf8 argument to WiiBt_Create lets the application choose where pairing keys are stored. When it is null or empty, the default Windows path is:

~~~text
%LOCALAPPDATA%\WiiMoteDLL_Libusbk\wii_link_keys.txt
~~~

The file contains Bluetooth addresses and link keys. Treat it as a credential; do not publish it or include it in bug reports. Applications can provide a path inside their own per-user data directory to keep keys separate.

## Build

Requirements: Windows 10/11, CMake 3.20+, a C++17 compiler, and a libusb-supported driver on the adapter. CMake fetches the libusb CMake project when the parent application has not already provided the usb-1.0 target.

~~~powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
~~~

Build x86 with -A Win32. Targets include:

- wii_bt_passthrough: reusable wii_bt_passthrough.dll.
- wii_discovery: standalone sample/diagnostic client linked to the public API.
- wii_bt_api_test: hardware-independent ABI, adapter-enumeration and lifecycle test.

The runtime package contains the host DLL, libusb-1.0.dll, the sample client, headers, README and licenses. Both runtime DLLs must match the consuming application's architecture.

## Diagnostics

Run wii_discovery.exe to enumerate adapters and begin scanning with SYNC pairing. Select a specific adapter or 1+2 pairing as follows:

~~~bat
wii_discovery.exe --vid 0x0A12 --pid 0x0001
wii_discovery.exe --pairing 1+2
~~~

The original direct-address protocol probe is preserved at tools/wii_discovery_protocol_probe.cpp for low-level hardware investigation. The normal CLI and consuming applications use the same host DLL.

## Hardware validation status

The SYNC path has been tested with a real Wii Remote and CSR8510 A10 on Windows using libusbK. The physical test completed inquiry, baseband connection, legacy PIN authentication, link encryption, both HID L2CAP channel setups and real core-button report reception. The HOTD2RE runtime also implements persistent two-remote operation and report 0x33 IR handling; the shared DLL build still needs a fresh physical regression run after extraction.

That hardware result does not guarantee compatibility with every USB Bluetooth adapter. Additional chipsets, Windows driver combinations, 1+2 pairing and reconnect behavior need community testing.

## License

Original code in this repository is released under **0BSD** (see LICENSE). This permissive, OSI-approved license allows use, modification, redistribution and incorporation into other projects, including closed-source and commercial applications, without an attribution condition.

The implementation is inspired by Dolphin's Bluetooth passthrough feature, but this repository does not copy or relicense Dolphin source. Any future Dolphin-derived code must retain its GPL-2.0-or-later license and notices and must be reviewed separately; it cannot be relicensed as 0BSD.

Third-party dependencies retain their own licenses. In particular, libusb remains LGPL-2.1-or-later; the 0BSD grant applies only to this repository's original code.
