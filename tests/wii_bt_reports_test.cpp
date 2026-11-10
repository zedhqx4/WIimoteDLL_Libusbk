#include "wii_bt_internal.h"

#include <array>
#include <cstdint>

int main() {
    using namespace input::wii;

    Remote remote{};
    const std::array<uint8_t, 3> buttons{0x30, 0x00, 0x08}; // A pressed
    if (!DecodeReport(buttons.data(), buttons.size(), remote))
        return 1;
    if (remote.buttons != 0x0008 || remote.pressed != 0x0008)
        return 2;

    const std::array<uint8_t, 3> release{0x30, 0x00, 0x00};
    if (!DecodeReport(release.data(), release.size(), remote))
        return 3;
    if (remote.buttons != 0 || remote.pressed != 0x0008)
        return 4;

    const std::array<uint8_t, 7> status{0x20, 0, 0, 0, 0, 0, 0x60};
    if (!DecodeReport(status.data(), status.size(), remote) || remote.battery != 50)
        return 5;

    const std::array<uint8_t, 18> ir{
        0x33, 0, 0, 0, 0, 0,
        100, 100, 0, 200, 100, 0,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    if (!DecodeReport(ir.data(), ir.size(), remote) || !remote.tracking || remote.dots != 2)
        return 6;

    const std::array<uint8_t, 2> payload{0xaa, 0x55};
    const auto write = WriteRegister(0xA1B2C3, payload.data(), payload.size());
    if (write[0] != 0x16 || write[1] != 0x04 ||
        write[2] != 0xA1 || write[3] != 0xB2 || write[4] != 0xC3 ||
        write[5] != 2 || write[6] != 0xaa || write[7] != 0x55)
        return 7;

    if (DecodeReport(buttons.data(), 2, remote))
        return 8;
    return 0;
}
