#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace input::wii {
struct Point { double x = 0.5; double y = 0.5; };
struct Remote {
    bool connected = false;
    bool reserved = false;
    bool tracking = false;
    uint16_t buttons = 0;
    uint16_t pressed = 0;
    int battery = -1;
    int dots = 0;
    Point raw{};
};
struct Snapshot {
    std::array<Remote, 2> player{};
    bool scanning = false;
    std::string message = "Select Connect and press the red SYNC button";
};
class Backend {
public:
    virtual ~Backend() = default;
    virtual void Enable(bool enabled, bool continuous) = 0;
    virtual void Scan() = 0;
    virtual void Disconnect(int player) = 0;
    virtual void SwapPlayers() = 0;
    virtual Snapshot Poll() = 0;
};
} // namespace input::wii
