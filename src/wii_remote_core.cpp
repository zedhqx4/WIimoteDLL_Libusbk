#include "wii_remote_core.h"

#include <algorithm>
#include <cmath>

namespace input::wii {

bool DecodeReport(const uint8_t* bytes, size_t size, Remote& remote) {
    if (!bytes || size < 3)
        return false;

    size_t required = 0;
    switch (bytes[0]) {
    case 0x20: required = 7; break;
    case 0x21: required = 22; break;
    case 0x22: required = 5; break;
    case 0x30: required = 3; break;
    case 0x31: required = 6; break;
    case 0x33: required = 18; break;
    default: return false;
    }
    if (size < required)
        return false;

    const uint16_t buttons = static_cast<uint16_t>(
        ((static_cast<uint16_t>(bytes[1]) << 8) | bytes[2]) & 0x1F9F);
    remote.pressed |= static_cast<uint16_t>(buttons & ~remote.buttons);
    remote.buttons = buttons;

    if (bytes[0] == 0x20)
        remote.battery = std::clamp(static_cast<int>(bytes[6]) * 100 / 0xC0, 0, 100);

    if (bytes[0] == 0x33) {
        std::array<Point, 4> dots{};
        int count = 0;
        for (int i = 0; i < 4; ++i) {
            const uint8_t* dot = bytes + 6 + i * 3;
            const int x = dot[0] + ((dot[2] & 0x30) << 4);
            const int y = dot[1] + ((dot[2] & 0xC0) << 2);
            if (x < 1023 && y < 768)
                dots[count++] = {static_cast<double>(x), static_cast<double>(y)};
        }

        remote.dots = count;
        remote.tracking = count >= 2;
        if (remote.tracking) {
            int left = 0;
            int right = 1;
            double best = -1.0;
            for (int i = 0; i < count; ++i) {
                for (int j = i + 1; j < count; ++j) {
                    const double dx = dots[i].x - dots[j].x;
                    const double dy = dots[i].y - dots[j].y;
                    const double distance = dx * dx + dy * dy;
                    if (distance > best) {
                        best = distance;
                        left = i;
                        right = j;
                    }
                }
            }
            if (best < 16.0) {
                remote.tracking = false;
                return true;
            }
            remote.raw = {
                1.0 - (dots[left].x + dots[right].x) / (2.0 * 1023.0),
                (dots[left].y + dots[right].y) / (2.0 * 767.0)
            };
        }
    }

    return true;
}

std::array<uint8_t, 22> WriteRegister(
    uint32_t address, const uint8_t* bytes, size_t size) {
    std::array<uint8_t, 22> report{};
    if (!bytes || size == 0 || size > 16)
        return report;

    report[0] = 0x16;
    report[1] = 0x04;
    report[2] = static_cast<uint8_t>(address >> 16);
    report[3] = static_cast<uint8_t>(address >> 8);
    report[4] = static_cast<uint8_t>(address);
    report[5] = static_cast<uint8_t>(size);
    std::copy(bytes, bytes + size, report.begin() + 6);
    return report;
}

} // namespace input::wii
