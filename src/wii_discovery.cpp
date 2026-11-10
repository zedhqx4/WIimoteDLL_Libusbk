#include "wii_bt_api.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
bool ParseId(const char* text, int32_t& value) {
    if (!text || !*text)
        return false;
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 0);
    if (end == text || *end != '\0' || parsed < 1 || parsed > 0xFFFF)
        return false;
    value = static_cast<int32_t>(parsed);
    return true;
}

void PrintButtons(uint16_t buttons) {
    struct Item { uint16_t mask; const char* name; };
    static constexpr Item items[] = {
        {0x0001, "2"}, {0x0002, "1"}, {0x0004, "B"}, {0x0008, "A"},
        {0x0010, "Minus"}, {0x0080, "Home"}, {0x0100, "Left"},
        {0x0200, "Right"}, {0x0400, "Down"}, {0x0800, "Up"}, {0x1000, "Plus"}
    };
    bool first = true;
    for (const auto& item : items) {
        if ((buttons & item.mask) == 0)
            continue;
        std::printf("%s%s", first ? "" : " + ", item.name);
        first = false;
    }
    if (first)
        std::printf("none");
}
}

int main(int argc, char** argv) {
    int32_t vid = -1;
    int32_t pid = -1;
    int32_t pairingMode = WII_BT_PAIRING_SYNC;
    bool idsSet = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--vid") == 0 && i + 1 < argc) {
            if (!ParseId(argv[++i], vid)) {
                std::fprintf(stderr, "Invalid VID. Use decimal or 0x-prefixed hexadecimal.\n");
                return 2;
            }
            idsSet = true;
        } else if (std::strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            if (!ParseId(argv[++i], pid)) {
                std::fprintf(stderr, "Invalid PID. Use decimal or 0x-prefixed hexadecimal.\n");
                return 2;
            }
            idsSet = true;
        } else if (std::strcmp(argv[i], "--pairing") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (std::strcmp(mode, "sync") == 0)
                pairingMode = WII_BT_PAIRING_SYNC;
            else if (std::strcmp(mode, "1+2") == 0)
                pairingMode = WII_BT_PAIRING_ONE_PLUS_TWO;
            else {
                std::fprintf(stderr, "Pairing mode must be sync or 1+2.\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: wii_discovery [--vid VID --pid PID] [--pairing sync|1+2]\n"
                      "Default adapter selection: Automatic\n"
                      "The tool continuously scans and prints Wii Remote button/IR status.");
            return 0;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\nUse --help for usage.\n", argv[i]);
            return 2;
        }
    }

    if (idsSet && (vid == -1 || pid == -1)) {
        std::fprintf(stderr, "Specify both --vid and --pid, or neither for Automatic.\n");
        return 2;
    }

    const int32_t adapterCount = WiiBt_EnumerateAdapters(nullptr, 0);
    if (adapterCount < 0) {
        std::fprintf(stderr, "USB adapter enumeration failed.\n");
        return 1;
    }
    std::vector<WiiBtAdapterInfo> adapters(static_cast<size_t>(adapterCount));
    if (adapterCount > 0 &&
        WiiBt_EnumerateAdapters(adapters.data(), adapterCount) < 0) {
        std::fprintf(stderr, "Could not read USB adapter information.\n");
        return 1;
    }

    std::puts("Wii Remote USB Bluetooth host");
    std::printf("Claimable standard HCI adapters: %d\n", adapterCount);
    for (const auto& adapter : adapters) {
        std::printf("  %04X:%04X  bus %u device %u  %s %s\n",
                    adapter.vid, adapter.pid, adapter.bus, adapter.address,
                    adapter.manufacturer, adapter.product);
    }

    if (!WiiBt_HasCompatibleAdapter(vid, pid)) {
        if (idsSet)
            std::fprintf(stderr, "Selected adapter %04X:%04X is not claimable.\n",
                         static_cast<unsigned>(vid), static_cast<unsigned>(pid));
        else
            std::fprintf(stderr, "No claimable USB Bluetooth HCI adapter found.\n");
        return 3;
    }

    void* host = WiiBt_Create(vid, pid, pairingMode, nullptr);
    if (!host) {
        std::fprintf(stderr, "Could not create the Bluetooth host.\n");
        return 4;
    }
    if (!WiiBt_Enable(host, 1, 1) || !WiiBt_Scan(host)) {
        std::fprintf(stderr, "Could not start Bluetooth discovery.\n");
        WiiBt_Destroy(host);
        return 5;
    }

    std::puts(pairingMode == WII_BT_PAIRING_SYNC
        ? "Scanning. Press the red SYNC button on the Wii Remote."
        : "Scanning. Press 1+2 on the Wii Remote.");
    std::puts("Press Ctrl+C to exit.");

    std::array<uint16_t, 2> previousButtons{0xFFFF, 0xFFFF};
    std::string previousMessage;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (std::chrono::steady_clock::now() < deadline) {
        WiiBtSnapshot snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        if (!WiiBt_Poll(host, &snapshot)) {
            std::fprintf(stderr, "Bluetooth host polling failed.\n");
            break;
        }
        if (snapshot.message[0] != '\0' && previousMessage != snapshot.message) {
            std::printf("%s\n", snapshot.message);
            previousMessage = snapshot.message;
        }
        for (int player = 0; player < 2; ++player) {
            const auto& remote = snapshot.player[player];
            if (!remote.connected)
                continue;
            if (remote.buttons != previousButtons[player]) {
                std::printf("P%d buttons: ", player + 1);
                PrintButtons(remote.buttons);
                std::printf("\n");
                previousButtons[player] = remote.buttons;
            }
            if (remote.tracking)
                std::printf("P%d IR: %.3f, %.3f (%u dots)\n",
                            player + 1, remote.raw_x, remote.raw_y, remote.dots);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    WiiBt_Enable(host, 0, 0);
    WiiBt_Destroy(host);
    return 0;
}
