# Third-party code notices

## HOTD2RE USB Wii Remote passthrough backend

`src/wii_bt_passthrough.cpp` is extracted from the Windows USB Bluetooth passthrough
backend in [GitFist/hotd2-re](https://github.com/GitFist/hotd2-re). That source is
licensed under the MIT License and remains MIT-licensed here. The complete license
notice is preserved in [LICENSE-HOTD2RE](LICENSE-HOTD2RE).

The independently developed HCI discovery/pairing diagnostic in
`src/wii_discovery.cpp` is covered by this repository's 0BSD license.
libusb is a separate dependency and retains its own LGPL-2.1-or-later license.
