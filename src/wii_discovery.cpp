#include <libusb.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
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
    uint8_t acl_in_endpoint = 0;
    uint8_t acl_out_endpoint = 0;
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

            uint8_t event_endpoint = 0;
            uint8_t acl_in_endpoint = 0;
            uint8_t acl_out_endpoint = 0;
            for (uint8_t e = 0; e < alt.bNumEndpoints; ++e)
            {
                const auto& ep = alt.endpoint[e];
                const auto type = ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                const bool input = (ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN;
                if (type == LIBUSB_TRANSFER_TYPE_INTERRUPT && input)
                    event_endpoint = ep.bEndpointAddress;
                else if (type == LIBUSB_TRANSFER_TYPE_BULK && input)
                    acl_in_endpoint = ep.bEndpointAddress;
                else if (type == LIBUSB_TRANSFER_TYPE_BULK && !input)
                    acl_out_endpoint = ep.bEndpointAddress;
            }
            if (event_endpoint && acl_in_endpoint && acl_out_endpoint)
            {
                result = {alt.bInterfaceNumber, alt.bAlternateSetting,
                          event_endpoint, acl_in_endpoint, acl_out_endpoint};
                found = true;
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


bool SendAcl(libusb_device_handle* handle, uint8_t endpoint, uint16_t handle_id,
             uint16_t cid, const uint8_t* payload, uint16_t payload_size)
{
    std::vector<uint8_t> packet(8 + payload_size);
    const uint16_t hf = static_cast<uint16_t>((handle_id & 0x0FFF) | 0x2000);
    const uint16_t length = static_cast<uint16_t>(4 + payload_size);
    packet[0] = static_cast<uint8_t>(hf);
    packet[1] = static_cast<uint8_t>(hf >> 8);
    packet[2] = static_cast<uint8_t>(length);
    packet[3] = static_cast<uint8_t>(length >> 8);
    packet[4] = static_cast<uint8_t>(payload_size);
    packet[5] = static_cast<uint8_t>(payload_size >> 8);
    packet[6] = static_cast<uint8_t>(cid);
    packet[7] = static_cast<uint8_t>(cid >> 8);
    std::copy(payload, payload + payload_size, packet.begin() + 8);
    int transferred = 0;
    const int rc = libusb_bulk_transfer(handle, endpoint, packet.data(),
        static_cast<int>(packet.size()), &transferred, 5000);
    return rc == LIBUSB_SUCCESS && transferred == static_cast<int>(packet.size());
}

bool ReadAcl(libusb_device_handle* handle, uint8_t endpoint, uint16_t expected_handle,
             std::vector<uint8_t>& l2cap, unsigned timeout_ms)
{
    std::array<uint8_t, 4096> packet{};
    int transferred = 0;
    const int rc = libusb_bulk_transfer(handle, endpoint, packet.data(),
        static_cast<int>(packet.size()), &transferred, timeout_ms);
    if (rc != LIBUSB_SUCCESS || transferred < 8)
        return false;
    const uint16_t hf = static_cast<uint16_t>(packet[0]) |
                        (static_cast<uint16_t>(packet[1]) << 8);
    const uint16_t acl_length = static_cast<uint16_t>(packet[2]) |
                                (static_cast<uint16_t>(packet[3]) << 8);
    const uint16_t l2cap_length = static_cast<uint16_t>(packet[4]) |
                                  (static_cast<uint16_t>(packet[5]) << 8);
    if ((hf & 0x0FFF) != expected_handle ||
        acl_length + 4u > static_cast<unsigned>(transferred) ||
        l2cap_length + 4u > acl_length ||
        l2cap_length + 8u > static_cast<unsigned>(transferred))
        return false;
    l2cap.assign(packet.begin() + 4, packet.begin() + 8 + l2cap_length);
    return true;
}

bool ConnectL2capPsm(libusb_device_handle* handle, const HciInterface& hci,
                     uint16_t handle_id, uint16_t psm, uint16_t local_cid,
                     uint16_t& remote_cid)
{
    const uint8_t ident = psm == 0x0011 ? 1 : 2;
    const std::array<uint8_t, 8> req{
        0x02, ident, 0x04, 0x00, static_cast<uint8_t>(psm),
        static_cast<uint8_t>(psm >> 8), static_cast<uint8_t>(local_cid),
        static_cast<uint8_t>(local_cid >> 8)
    };
    if (!SendAcl(handle, hci.acl_out_endpoint, handle_id, 0x0001,
                 req.data(), static_cast<uint16_t>(req.size())))
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::vector<uint8_t> packet;
        if (!ReadAcl(handle, hci.acl_in_endpoint, handle_id, packet, 3000))
            continue;
        if (packet.size() < 16 || packet[2] != 0x01 ||
            packet[4] != 0x03 || packet[5] != ident)
            continue;
        const uint16_t destination_cid = static_cast<uint16_t>(packet[8]) |
                                         (static_cast<uint16_t>(packet[9]) << 8);
        const uint16_t echoed_cid = static_cast<uint16_t>(packet[10]) |
                                    (static_cast<uint16_t>(packet[11]) << 8);
        const uint16_t result = static_cast<uint16_t>(packet[12]) |
                                (static_cast<uint16_t>(packet[13]) << 8);
        const uint16_t status = static_cast<uint16_t>(packet[14]) |
                                (static_cast<uint16_t>(packet[15]) << 8);
        if (echoed_cid != local_cid)
            continue;

        // L2CAP result 0x0001 means Pending, not rejection. The peer may
        // send another Connection Response with the same identifier later.
        if (result == 0x0001)
        {
            std::printf("L2CAP PSM 0x%04X connection pending (status 0x%04X).\n",
                        psm, status);
            continue;
        }
        if (result != 0)
        {
            const char* reason = result == 0x0002 ? "PSM not supported" :
                                 result == 0x0003 ? "security block" :
                                 result == 0x0004 ? "no resources" : "unknown result";
            std::printf("L2CAP PSM 0x%04X connection failed: %s (result 0x%04X, status 0x%04X).\n",
                        psm, reason, result, status);
            return false;
        }
        remote_cid = destination_cid;
        std::printf("L2CAP PSM 0x%04X connected (local 0x%04X, remote 0x%04X).\n",
                    psm, local_cid, remote_cid);
        return true;
    }
    std::printf("L2CAP PSM 0x%04X response timeout.\n", psm);
    return false;
}


struct L2capChannel
{
    uint16_t psm = 0;
    uint16_t local_cid = 0;
    uint16_t remote_cid = 0;
    uint8_t config_identifier = 0;
    bool config_response_received = false;
    bool peer_config_received = false;
};

uint16_t ReadLe16(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

void AppendLe16(std::vector<uint8_t>& data, uint16_t value)
{
    data.push_back(static_cast<uint8_t>(value));
    data.push_back(static_cast<uint8_t>(value >> 8));
}

bool SendL2capSignal(libusb_device_handle* handle, const HciInterface& hci,
                     uint16_t handle_id, uint8_t code, uint8_t identifier,
                     const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> signal{code, identifier};
    AppendLe16(signal, static_cast<uint16_t>(body.size()));
    signal.insert(signal.end(), body.begin(), body.end());
    return SendAcl(handle, hci.acl_out_endpoint, handle_id, 0x0001,
                   signal.data(), static_cast<uint16_t>(signal.size()));
}

bool SendConfigurationRequest(libusb_device_handle* handle, const HciInterface& hci,
                              uint16_t handle_id, L2capChannel& channel)
{
    std::vector<uint8_t> body;
    AppendLe16(body, channel.remote_cid);
    AppendLe16(body, 0);
    return SendL2capSignal(handle, hci, handle_id, 0x04,
                           channel.config_identifier, body);
}

bool HandleConfigurationRequest(libusb_device_handle* handle, const HciInterface& hci,
                                 uint16_t handle_id, const std::vector<uint8_t>& packet,
                                 L2capChannel& control, L2capChannel& interrupt)
{
    if (packet.size() < 12 || packet[4] != 0x04)
        return false;
    const uint8_t identifier = packet[5];
    const uint16_t command_length = ReadLe16(packet, 6);
    if (command_length < 4 || packet.size() < 8u + command_length)
        return false;
    const uint16_t destination_cid = ReadLe16(packet, 8);
    const uint16_t flags = ReadLe16(packet, 10);
    if (flags != 0)
        return false;

    L2capChannel* channel = destination_cid == control.local_cid ? &control :
                            destination_cid == interrupt.local_cid ? &interrupt : nullptr;
    if (!channel)
        return false;

    size_t offset = 12;
    const size_t end = 8u + command_length;
    std::vector<uint8_t> unknown_options;
    while (offset < end)
    {
        if (end - offset < 2)
            return false;
        const uint8_t type = packet[offset];
        const uint8_t option_length = packet[offset + 1];
        if (offset + 2u + option_length > end)
            return false;
        const uint8_t option_type = type & 0x7F;
        if (option_type == 0x01)
        {
            if (option_length != 2 || ReadLe16(packet, offset + 2) < 48)
                return false;
        }
        else if ((type & 0x80) == 0)
        {
            unknown_options.insert(unknown_options.end(),
                packet.begin() + static_cast<std::ptrdiff_t>(offset),
                packet.begin() + static_cast<std::ptrdiff_t>(offset + 2u + option_length));
        }
        offset += 2u + option_length;
    }

    std::vector<uint8_t> response;
    AppendLe16(response, channel->local_cid);
    AppendLe16(response, 0);
    AppendLe16(response, unknown_options.empty() ? 0 : 3);
    response.insert(response.end(), unknown_options.begin(), unknown_options.end());
    if (!SendL2capSignal(handle, hci, handle_id, 0x05, identifier, response))
        return false;
    if (unknown_options.empty())
    {
        channel->peer_config_received = true;
        std::printf("L2CAP PSM 0x%04X peer configuration accepted.\n", channel->psm);
    }
    else
        std::printf("L2CAP PSM 0x%04X requested unknown mandatory options; rejected for retry.\n",
                    channel->psm);
    return true;
}

bool ConfigureL2capChannels(libusb_device_handle* handle, const HciInterface& hci,
                            uint16_t handle_id, uint16_t control_local,
                            uint16_t control_remote, uint16_t interrupt_local,
                            uint16_t interrupt_remote)
{
    L2capChannel control{0x0011, control_local, control_remote, 3};
    L2capChannel interrupt{0x0013, interrupt_local, interrupt_remote, 4};
    if (!SendConfigurationRequest(handle, hci, handle_id, control) ||
        !SendConfigurationRequest(handle, hci, handle_id, interrupt))
    {
        std::puts("Could not send L2CAP Configuration Request.");
        return false;
    }
    std::puts("L2CAP configuration requests sent for HID Control and HID Interrupt.");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (control.config_response_received && control.peer_config_received &&
            interrupt.config_response_received && interrupt.peer_config_received)
        {
            std::puts("Both HID L2CAP channels are fully configured.");
            return true;
        }

        std::vector<uint8_t> packet;
        if (!ReadAcl(handle, hci.acl_in_endpoint, handle_id, packet, 1000))
            continue;
        if (packet.size() < 8 || ReadLe16(packet, 2) != 0x0001)
            continue;
        const uint8_t code = packet[4];
        const uint8_t identifier = packet[5];
        const uint16_t command_length = ReadLe16(packet, 6);
        if (packet.size() < 8u + command_length)
            continue;

        if (code == 0x04)
        {
            if (!HandleConfigurationRequest(handle, hci, handle_id, packet, control, interrupt))
                std::printf("Ignoring malformed or unrelated L2CAP Configuration Request (id %u).\n",
                            identifier);
        }
        else if (code == 0x05 && command_length >= 6)
        {
            const uint16_t source_cid = ReadLe16(packet, 8);
            const uint16_t flags = ReadLe16(packet, 10);
            const uint16_t result = ReadLe16(packet, 12);
            L2capChannel* channel = identifier == control.config_identifier ? &control :
                                    identifier == interrupt.config_identifier ? &interrupt : nullptr;
            if (!channel)
                continue;
            // Configuration Response SCID identifies the endpoint that sent
            // our Configuration Request: it must echo our local CID.
            if (source_cid != channel->local_cid || flags != 0)
            {
                std::printf("L2CAP PSM 0x%04X Configuration Response invalid: id=%u, "
                            "source CID=0x%04X (expected local 0x%04X), "
                            "remote CID=0x%04X, flags=0x%04X, result=0x%04X.\\n",
                            channel->psm, identifier, source_cid, channel->local_cid,
                            channel->remote_cid, flags, result);
                continue;
            }
            if (result != 0)
            {
                std::printf("L2CAP PSM 0x%04X Configuration Response failed (result 0x%04X).\n",
                            channel->psm, result);
                return false;
            }
            channel->config_response_received = true;
            std::printf("L2CAP PSM 0x%04X local configuration accepted.\n", channel->psm);
        }
        else if (code == 0x01)
            std::printf("L2CAP Command Reject received (identifier %u).\n", identifier);
    }

    std::printf("L2CAP configuration timeout: control(local=%d, peer=%d), interrupt(local=%d, peer=%d).\n",
        control.config_response_received, control.peer_config_received,
        interrupt.config_response_received, interrupt.peer_config_received);
    return false;
}

void PrintAddress(const uint8_t* address)
{
    std::printf("%02X:%02X:%02X:%02X:%02X:%02X",
                address[5], address[4], address[3],
                address[2], address[1], address[0]);
}


bool ParseAddress(const char* text, std::array<uint8_t, 6>& address)
{
    unsigned values[6]{};
    char tail = '\0';
    if (std::sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c",
                    &values[0], &values[1], &values[2],
                    &values[3], &values[4], &values[5], &tail) != 6)
        return false;
    for (size_t i = 0; i < address.size(); ++i)
        address[i] = static_cast<uint8_t>(values[5 - i]);
    return true;
}

bool RunConnection(libusb_device* device, const std::array<uint8_t, 6>& remote,
                   bool sync_pairing)
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
        goto connection_cleanup;
    }

    {
        HciEventPump pump(handle, hci.event_endpoint);
        std::vector<uint8_t> response;
        if (!SendCommand(handle, pump, 0x0C03, nullptr, 0, response))
        {
            std::puts("HCI Reset failed.");
            goto connection_cleanup;
        }
        std::puts("HCI Reset: OK");

        // Read the adapter's own Bluetooth address. HCI returns address bytes
        // in little-endian order, which is also the order required by PIN replies.
        if (!SendCommand(handle, pump, 0x1009, nullptr, 0, response) ||
            response.size() < 12 || response[5] != 0)
        {
            std::puts("HCI Read BD_ADDR failed.");
            goto connection_cleanup;
        }
        std::array<uint8_t, 6> local{};
        std::copy_n(response.begin() + 6, local.size(), local.begin());
        std::printf("Host Bluetooth address: ");
        PrintAddress(local.data());
        std::printf("\nTarget Wii Remote: ");
        PrintAddress(remote.data());
        std::printf("\nPairing mode: %s\n", sync_pairing ? "SYNC (bonding)" : "1+2 (temporary)");

        // Create Connection: packet type DM1/DH1/DM3/DH3/DM5/DH5,
        // page scan repetition R1, clock offset unknown, role switch allowed.
        std::array<uint8_t, 13> create{};
        std::copy(remote.begin(), remote.end(), create.begin());
        create[6] = 0x18;
        create[7] = 0xCC;
        create[8] = 0x01;
        create[12] = 0x01;
        if (!SendCommand(handle, pump, 0x0405, create.data(),
                         static_cast<uint8_t>(create.size()), response))
        {
            std::puts("HCI Create Connection was rejected.");
            goto connection_cleanup;
        }
        std::puts("Create Connection accepted; waiting for the baseband link...");

        uint16_t connection_handle = 0;
        bool connected = false;
        while (!connected)
        {
            if (!pump.ReadNext(response, 20000))
            {
                std::puts("Timed out waiting for Connection Complete.");
                goto connection_cleanup;
            }
            if (response[0] == 0x03 && response.size() >= 13)
            {
                if (!std::equal(remote.begin(), remote.end(), response.begin() + 5))
                    continue;
                if (response[2] != 0)
                {
                    std::printf("Connection failed: HCI status 0x%02X\n", response[2]);
                    goto connection_cleanup;
                }
                connection_handle = static_cast<uint16_t>(response[3]) |
                                    (static_cast<uint16_t>(response[4] & 0x0F) << 8);
                connected = true;
                std::printf("Baseband connection established (handle 0x%04X).\n",
                            connection_handle);
            }
            else if (response[0] == 0x04 && response.size() >= 12)
            {
                // A remote-initiated connection can race with our Create Connection.
                if (std::equal(remote.begin(), remote.end(), response.begin() + 2))
                {
                    const std::array<uint8_t, 7> accept{
                        remote[0], remote[1], remote[2], remote[3], remote[4], remote[5], 0x00
                    };
                    std::vector<uint8_t> ignored;
                    if (!SendCommand(handle, pump, 0x0409, accept.data(),
                                     static_cast<uint8_t>(accept.size()), ignored))
                        std::puts("Warning: Accept Connection Request failed.");
                }
            }
            else
            {
                std::printf("HCI event 0x%02X while waiting for connection.\n", response[0]);
            }
        }

        // Authentication is explicitly requested after the ACL/baseband link exists.
        const std::array<uint8_t, 2> auth_parameters{
            static_cast<uint8_t>(connection_handle & 0xFF),
            static_cast<uint8_t>(connection_handle >> 8)
        };
        if (!SendCommand(handle, pump, 0x0411, auth_parameters.data(),
                         static_cast<uint8_t>(auth_parameters.size()), response))
        {
            std::puts("HCI Authentication Requested was rejected.");
            goto connection_cleanup;
        }
        std::puts("Authentication requested; handling Wii Remote pairing events...");

        bool authenticated = false;
        while (!authenticated)
        {
            if (!pump.ReadNext(response, 15000))
            {
                std::puts("Timed out waiting for Authentication Complete.");
                goto connection_cleanup;
            }

            if (response[0] == 0x16 && response.size() >= 8)
            {
                // Legacy PIN is six raw address bytes, not six ASCII digits.
                // SYNC bonds to the host address; 1+2 uses the remote address.
                const auto& pin_address = sync_pairing ? local : remote;
                std::array<uint8_t, 23> pin_reply{};
                std::copy(response.begin() + 2, response.begin() + 8, pin_reply.begin());
                pin_reply[6] = 6;
                std::copy(pin_address.begin(), pin_address.end(), pin_reply.begin() + 7);
                std::vector<uint8_t> pin_response;
                if (!SendCommand(handle, pump, 0x040D, pin_reply.data(),
                                 static_cast<uint8_t>(pin_reply.size()), pin_response))
                {
                    std::puts("PIN Code Request Reply failed.");
                    goto connection_cleanup;
                }
                std::puts("Automatic legacy pairing response sent.");
            }
            else if (response[0] == 0x17 && response.size() >= 8)
            {
                // No stored link key yet: ask the remote to use legacy PIN pairing.
                std::array<uint8_t, 6> address{};
                std::copy(response.begin() + 2, response.begin() + 8, address.begin());
                std::vector<uint8_t> key_response;
                if (!SendCommand(handle, pump, 0x040C, address.data(),
                                 static_cast<uint8_t>(address.size()), key_response))
                {
                    std::puts("Link Key Request Negative Reply failed.");
                    goto connection_cleanup;
                }
                std::puts("No stored link key; requested legacy PIN authentication.");
            }
            else if (response[0] == 0x06 && response.size() >= 5)
            {
                const uint16_t event_handle = static_cast<uint16_t>(response[3]) |
                    (static_cast<uint16_t>(response[4] & 0x0F) << 8);
                if (event_handle != connection_handle)
                    continue;
                if (response[2] != 0)
                {
                    std::printf("Authentication failed: HCI status 0x%02X\n", response[2]);
                    goto connection_cleanup;
                }
                authenticated = true;
                std::puts("Authentication Complete: success.");
            }
            else if (response[0] == 0x18 && response.size() >= 25)
            {
                std::puts("Link Key Notification received (key not persisted by this diagnostic).");
            }
            else
            {
                std::printf("HCI event 0x%02X during authentication.\n", response[0]);
            }
        }

        std::puts("Baseband authentication is complete. Opening HID L2CAP channels...");
        uint16_t control_cid = 0;
        uint16_t interrupt_cid = 0;
        if (!ConnectL2capPsm(handle, hci, connection_handle, 0x0011, 0x0040, control_cid))
            goto connection_cleanup;
        if (!ConnectL2capPsm(handle, hci, connection_handle, 0x0013, 0x0041, interrupt_cid))
            goto connection_cleanup;
        if (!ConfigureL2capChannels(handle, hci, connection_handle,
                                    0x0040, control_cid, 0x0041, interrupt_cid))
            goto connection_cleanup;
        std::puts("HID L2CAP Control and Interrupt channels are ready for HIDP.");
        success = true;
    }

connection_cleanup:
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
    bool connect_mode = false;
    bool sync_pairing = true;
    std::array<uint8_t, 6> target{};

    if ((argc == 4 || argc == 6) && std::strcmp(argv[1], "--connect") == 0)
    {
        connect_mode = true;
        if (!ParseAddress(argv[2], target))
        {
            std::fprintf(stderr, "Invalid Bluetooth address: %s\n", argv[2]);
            return 2;
        }
        if (std::strcmp(argv[3], "sync") == 0)
            sync_pairing = true;
        else if (std::strcmp(argv[3], "1+2") == 0)
            sync_pairing = false;
        else
        {
            std::puts("Pairing mode must be 'sync' or '1+2'.");
            return 2;
        }
        if (argc == 6)
        {
            vid = static_cast<uint16_t>(std::strtoul(argv[4], nullptr, 0));
            pid = static_cast<uint16_t>(std::strtoul(argv[5], nullptr, 0));
        }
    }
    else if (argc == 3)
    {
        vid = static_cast<uint16_t>(std::strtoul(argv[1], nullptr, 0));
        pid = static_cast<uint16_t>(std::strtoul(argv[2], nullptr, 0));
    }
    else if (argc != 1)
    {
        std::printf("Usage:\n  %s [VID PID]\n  %s --connect BD_ADDR sync|1+2 [VID]\n",
                    argv[0], argv[0]);
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
        passed = connect_mode ? RunConnection(devices[i], target, sync_pairing)
                              : RunInquiry(devices[i]);
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
