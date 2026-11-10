#include <libusb.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

namespace
{
constexpr uint16_t kDefaultVid = 0x0A12;
constexpr uint16_t kDefaultPid = 0x0001;
constexpr uint8_t kUsbBluetoothClass = 0xE0;
constexpr uint8_t kUsbBluetoothSubclass = 0x01;
constexpr uint8_t kUsbBluetoothProtocol = 0x01;
constexpr uint8_t kEventCommandComplete = 0x0E;
constexpr uint8_t kEventCommandStatus = 0x0F;
constexpr uint8_t kEventInquiryComplete = 0x01;
constexpr uint8_t kEventInquiryResult = 0x02;
constexpr unsigned kTimeoutMs = 10000;

struct HciInterface
{
    int number = -1;
    int alternate = 0;
    uint8_t event_endpoint = 0;
};

bool FindHciInterface(libusb_device* device, HciInterface& result)
{
    libusb_config_descriptor* config = nullptr;
    const int rc = libusb_get_active_config_descriptor(device, &config);
    if (rc != LIBUSB_SUCCESS || !config)
        return false;

    bool found = false;
    for (uint8_t i = 0; i < config->bNumInterfaces && !found; ++i)
    {
        const auto& group = config->interface[i];
        for (int a = 0; a < group.num_altsetting && !found; ++a)
        {
            const auto& alt = group.altsetting[a];
            if (alt.bInterfaceClass != kUsbBluetoothClass ||
                alt.bInterfaceSubClass != kUsbBluetoothSubclass ||
                alt.bInterfaceProtocol != kUsbBluetoothProtocol)
                continue;

            for (uint8_t e = 0; e < alt.bNumEndpoints; ++e)
            {
                const auto& ep = alt.endpoint[e];
                const bool is_interrupt = (ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                                          LIBUSB_TRANSFER_TYPE_INTERRUPT;
                const bool is_input = (ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                                      LIBUSB_ENDPOINT_IN;
                if (is_interrupt && is_input)
                {
                    result = {alt.bInterfaceNumber, alt.bAlternateSetting,
                              ep.bEndpointAddress};
                    found = true;
                    break;
                }
            }
        }
    }

    libusb_free_config_descriptor(config);
    return found;
}

bool ReadEvent(libusb_device_handle* handle, uint8_t endpoint,
               std::vector<uint8_t>& event, unsigned timeout_ms)
{
    std::array<uint8_t, 260> buffer{};
    int transferred = 0;
    const int rc = libusb_interrupt_transfer(handle, endpoint, buffer.data(),
                                              static_cast<int>(buffer.size()),
                                              &transferred, timeout_ms);
    if (rc != LIBUSB_SUCCESS || transferred < 2)
    {
        std::fprintf(stderr, "HCI event read failed: %s (%d bytes)\n",
                     libusb_error_name(rc), transferred);
        return false;
    }

    const auto parameter_length = buffer[1];
    if (static_cast<unsigned>(transferred) < 2u + parameter_length)
    {
        std::fprintf(stderr, "Malformed HCI event: declared %u, received %d\n",
                     parameter_length, transferred);
        return false;
    }

    event.assign(buffer.begin(), buffer.begin() + transferred);
    return true;
}

class HciEventPump
{
public:
    HciEventPump(libusb_device_handle* handle, uint8_t event_endpoint)
        : handle_(handle), event_endpoint_(event_endpoint)
    {
    }

    bool ReadNext(std::vector<uint8_t>& event, unsigned timeout_ms)
    {
        if (!pending_.empty())
        {
            event = std::move(pending_.front());
            pending_.pop_front();
            return true;
        }
        return ReadEvent(handle_, event_endpoint_, event, timeout_ms);
    }

    bool WaitForCommandResponse(uint16_t opcode, std::vector<uint8_t>& response)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            const auto match = std::find_if(pending_.begin(), pending_.end(),
                [opcode](const std::vector<uint8_t>& event)
                {
                    return IsCommandResponse(event, opcode);
                });
            if (match != pending_.end())
            {
                response = std::move(*match);
                pending_.erase(match);
                return CommandSucceeded(response);
            }

            std::vector<uint8_t> event;
            if (!ReadEvent(handle_, event_endpoint_, event, kTimeoutMs))
                return false;

            if (IsCommandResponse(event, opcode))
            {
                response = std::move(event);
                return CommandSucceeded(response);
            }
            pending_.push_back(std::move(event));
        }

        std::fprintf(stderr, "No completion/status event for HCI opcode 0x%04x\n", opcode);
        return false;
    }

private:
    static bool IsCommandResponse(const std::vector<uint8_t>& event, uint16_t opcode)
    {
        if (event.empty())
            return false;
        if (event[0] == kEventCommandComplete && event.size() >= 6)
        {
            const uint16_t received = static_cast<uint16_t>(event[3]) |
                                      (static_cast<uint16_t>(event[4]) << 8);
            return received == opcode;
        }
        if (event[0] == kEventCommandStatus && event.size() >= 6)
        {
            const uint16_t received = static_cast<uint16_t>(event[4]) |
                                      (static_cast<uint16_t>(event[5]) << 8);
            return received == opcode;
        }
        return false;
    }

    static bool CommandSucceeded(const std::vector<uint8_t>& event)
    {
        if (event[0] == kEventCommandComplete)
            return event[5] == 0;
        if (event[0] == kEventCommandStatus)
            return event[2] == 0;
        return false;
    }

    libusb_device_handle* handle_;
    uint8_t event_endpoint_;
    std::deque<std::vector<uint8_t>> pending_;
};

bool SendCommand(libusb_device_handle* handle, HciEventPump& event_pump,
                 uint16_t opcode, const uint8_t* parameters, uint8_t parameter_length,
                 std::vector<uint8_t>& response)
{
    std::array<uint8_t, 258> packet{};
    packet[0] = static_cast<uint8_t>(opcode & 0xFF);
    packet[1] = static_cast<uint8_t>(opcode >> 8);
    packet[2] = parameter_length;
    if (parameter_length && parameters)
        std::memcpy(packet.data() + 3, parameters, parameter_length);

    // USB HCI commands travel over the default control pipe.
    const uint8_t request_type = LIBUSB_ENDPOINT_OUT |
                                 LIBUSB_REQUEST_TYPE_CLASS |
                                 LIBUSB_RECIPIENT_DEVICE;
    const int written = libusb_control_transfer(
        handle, request_type, 0, 0, 0, packet.data(),
        static_cast<uint16_t>(parameter_length + 3), kTimeoutMs);
    if (written != parameter_length + 3)
    {
        std::fprintf(stderr, "HCI command 0x%04x failed: %s (%d bytes)\n",
                     opcode, written < 0 ? libusb_error_name(written) : "short transfer",
                     written);
        return false;
    }

    return event_pump.WaitForCommandResponse(opcode, response);
}

void PrintAddress(const uint8_t* address)
{
    std::printf("%02X:%02X:%02X:%02X:%02X:%02X",
                address[5], address[4], address[3],
                address[2], address[1], address[0]);
}

bool RunInquiry(libusb_device* device)
{
    HciInterface hci;
    if (!FindHciInterface(device, hci))
    {
        std::puts("No standard USB Bluetooth HCI interface found.");
        return false;
    }

    libusb_device_handle* handle = nullptr;
    int rc = libusb_open(device, &handle);
    if (rc != LIBUSB_SUCCESS)
    {
        std::fprintf(stderr, "USB open failed: %s\n", libusb_error_name(rc));
        return false;
    }

    bool claimed = false;
    bool success = false;

    rc = libusb_claim_interface(handle, hci.number);
    if (rc != LIBUSB_SUCCESS)
    {
        std::fprintf(stderr, "Claim interface %d failed: %s\n",
                     hci.number, libusb_error_name(rc));
        libusb_close(handle);
        return false;
    }
    claimed = true;

    if (hci.alternate != 0 &&
        libusb_set_interface_alt_setting(handle, hci.number, hci.alternate) != LIBUSB_SUCCESS)
    {
        std::puts("Could not select HCI alternate setting.");
        goto cleanup;
    }

    {
        std::vector<uint8_t> event;
        HciEventPump event_pump(handle, hci.event_endpoint);
        if (!SendCommand(handle, event_pump, 0x0C03, nullptr, 0, event))
        {
            std::puts("HCI Reset failed.");
            goto cleanup;
        }
        std::puts("HCI Reset: OK");

        // General Inquiry Access Code, 10.24 seconds, unlimited responses.
        constexpr std::array<uint8_t, 5> inquiry_parameters{
            0x33, 0x8B, 0x9E, 0x08, 0x00
        };
        if (!SendCommand(handle, event_pump, 0x0401,
                         inquiry_parameters.data(),
                         static_cast<uint8_t>(inquiry_parameters.size()), event))
        {
            std::puts("HCI Inquiry command failed.");
            goto cleanup;
        }
        std::puts("Inquiry started. Press SYNC or 1+2 on the Wii Remote now.");

        bool inquiry_complete = false;
        while (!inquiry_complete)
        {
            if (!event_pump.ReadNext(event, 15000))
                goto cleanup;

            switch (event[0])
            {
            case kEventInquiryResult:
            {
                const uint8_t count = event[2];
                constexpr size_t kResultSize = 13;
                if (event.size() < 3 + static_cast<size_t>(count) * kResultSize)
                {
                    std::puts("Ignoring malformed Inquiry Result event.");
                    break;
                }

                for (uint8_t i = 0; i < count; ++i)
                {
                    const uint8_t* entry = event.data() + 3 + i * kResultSize;
                    std::printf("Discovered Bluetooth device: ");
                    PrintAddress(entry);
                    std::printf("  class=%02X%02X%02X\n", entry[10], entry[9], entry[8]);
                }
                break;
            }
            case kEventInquiryComplete:
                if (event.size() >= 3 && event[2] != 0)
                {
                    std::printf("Inquiry completed with HCI status 0x%02X\n", event[2]);
                    goto cleanup;
                }
                inquiry_complete = true;
                break;
            default:
                std::printf("HCI event 0x%02X (%u parameter bytes)\n",
                            event[0], event[1]);
                break;
            }
        }
    }

    success = true;

cleanup:
    if (claimed)
    {
        const int release_rc = libusb_release_interface(handle, hci.number);
        if (release_rc != LIBUSB_SUCCESS)
            std::fprintf(stderr, "Interface release warning: %s\n",
                         libusb_error_name(release_rc));
    }
    libusb_close(handle);
    return success;
}
} // namespace

int main(int argc, char** argv)
{
    uint16_t vid = kDefaultVid;
    uint16_t pid = kDefaultPid;
    if (argc == 3)
    {
        vid = static_cast<uint16_t>(std::strtoul(argv[1], nullptr, 0));
        pid = static_cast<uint16_t>(std::strtoul(argv[2], nullptr, 0));
    }
    else if (argc != 1)
    {
        std::printf("Usage: %s [VID PID]\n", argv[0]);
        return 2;
    }

    libusb_context* context = nullptr;
    int rc = libusb_init(&context);
    if (rc != LIBUSB_SUCCESS)
    {
        std::fprintf(stderr, "libusb_init failed: %s\n", libusb_error_name(rc));
        return 1;
    }

    libusb_device** devices = nullptr;
    const ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0)
    {
        std::fprintf(stderr, "USB enumeration failed: %s\n",
                     libusb_error_name(static_cast<int>(count)));
        libusb_exit(context);
        return 1;
    }

    bool found = false;
    bool passed = false;
    for (ssize_t i = 0; i < count; ++i)
    {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[i], &descriptor) != LIBUSB_SUCCESS ||
            descriptor.idVendor != vid || descriptor.idProduct != pid)
            continue;

        found = true;
        std::printf("Using USB Bluetooth adapter %04X:%04X\n", vid, pid);
        passed = RunInquiry(devices[i]);
        break;
    }

    libusb_free_device_list(devices, 1);
    libusb_exit(context);

    if (!found)
    {
        std::puts("Bluetooth adapter not found.");
        return 3;
    }
    return passed ? 0 : 4;
}
