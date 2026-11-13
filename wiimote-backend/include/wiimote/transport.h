#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wiimote {

// Platform transports exchange complete Wii Remote reports. The core does not
// know about file descriptors, Win32 handles, Bluetooth APIs, or SDL devices.
class Transport {
public:
    virtual ~Transport() = default;

    // Wait up to timeout_ms for one report. A successful empty report means
    // timeout; false means the device was disconnected or could not be read.
    virtual bool read(std::vector<uint8_t>& report, unsigned timeout_ms) = 0;

    // Send one output report (report ID first). false means the write failed.
    virtual bool write(std::span<const uint8_t> report) = 0;

    // Stable physical-device identifier when the platform can provide one.
    virtual std::string id() const { return {}; }
};

} // namespace wiimote
