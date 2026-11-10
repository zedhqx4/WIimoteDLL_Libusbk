#include "wii_bt_api.h"

#include <cstdio>
#include <vector>

int main() {
    if (WiiBt_GetApiVersion() != WII_BT_API_VERSION) {
        std::fprintf(stderr, "FAIL: public API version mismatch\n");
        return 1;
    }

    const int32_t count = WiiBt_EnumerateAdapters(nullptr, 0);
    if (count < 0) {
        std::fprintf(stderr, "FAIL: adapter enumeration failed\n");
        return 1;
    }

    std::vector<WiiBtAdapterInfo> adapters(static_cast<size_t>(count));
    if (count > 0 && WiiBt_EnumerateAdapters(adapters.data(), count) != count) {
        std::fprintf(stderr, "FAIL: adapter enumeration count changed\n");
        return 1;
    }
    for (const auto& adapter : adapters) {
        if (adapter.struct_size != sizeof(WiiBtAdapterInfo) ||
            adapter.vid == 0 || adapter.pid == 0) {
            std::fprintf(stderr, "FAIL: invalid adapter information record\n");
            return 1;
        }
    }

    if (WiiBt_Create(-1, 1, WII_BT_PAIRING_SYNC, nullptr) != nullptr) {
        std::fprintf(stderr, "FAIL: incomplete VID/PID pair was accepted\n");
        return 1;
    }
    if (WiiBt_Create(-1, -1, 99, nullptr) != nullptr) {
        std::fprintf(stderr, "FAIL: unsupported pairing mode was accepted\n");
        return 1;
    }

    void* host = WiiBt_Create(-1, -1, WII_BT_PAIRING_SYNC, nullptr);
    if (!host) {
        std::fprintf(stderr, "FAIL: could not create host instance\n");
        return 1;
    }
    if (!WiiBt_Enable(host, 0, 0)) {
        std::fprintf(stderr, "FAIL: could not disable newly created host\n");
        WiiBt_Destroy(host);
        return 1;
    }

    WiiBtSnapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    if (!WiiBt_Poll(host, &snapshot) ||
        snapshot.struct_size != sizeof(WiiBtSnapshot)) {
        std::fprintf(stderr, "FAIL: public snapshot contract\n");
        WiiBt_Destroy(host);
        return 1;
    }

    WiiBt_Destroy(host);
    std::puts("PASS: adapter enumeration, ABI v3, configuration validation and host lifecycle");
    return 0;
}
