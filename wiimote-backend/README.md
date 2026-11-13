# Wiimote Backend

A small, emulator-independent C++20 library for decoding Wii Remote reports and
turning IR / Nunchuk sensor data into normalized input values.

This first extraction is intentionally limited to the platform-independent
core. It reuses the protocol and math implementation from
[`zedhqx4/reuc-recomp`](https://github.com/zedhqx4/reuc-recomp), keeping the
existing tested report behavior while moving it behind a public
`wiimote::` namespace and standalone CMake target.

## Included

- Wii Remote input report parsing and report encoding.
- Status, LED, reporting-mode, register-write, and initialization reports.
- Button state, IR camera dots, accelerometer and Nunchuk state.
- Absolute IR pointer calculation, Nunchuk stick normalization and swing
  detection.
- A small transport interface which platform backends can implement.

## Build and test

```sh
cmake -S wiimote-backend -B build-wiimote -DWIIMOTE_BUILD_TESTS=ON
cmake --build build-wiimote
ctest --test-dir build-wiimote --output-on-failure
```

The library itself has no SDL, GUI, emulator, or operating-system dependency.
It requires C++20.

## Transport status

| Platform | Current implementation | Status |
|---|---|---|
| Windows | Existing Win32 HID / SetupAPI transport in the parent project | Not yet moved into this library |
| Linux | Existing kernel `hid-wiimote` + evdev transport in the parent project | Not yet moved into this library |
| macOS | Not implemented | Research and implementation required |

The transport interface intentionally does not prescribe how a platform opens
Bluetooth HID devices. In particular, Linux currently uses the kernel
`hid-wiimote` driver and evdev nodes; it is not a direct BlueZ implementation.
The next extraction step will move the platform transports behind this API
without changing the report parser or input math.

## Emulator integration

Emulator adapters should consume `wiimote::WiiState` and normalized IR /
Nunchuk values directly. They must not create a virtual mouse or feed operating
system mouse events. Each emulator adapter remains responsible for translating
the shared state into its native GunCon / lightgun input model.

## License

GPL-3.0-only. The source was extracted from the GPL-3.0 project
`reuc-recomp`; see [LICENSE](LICENSE).
