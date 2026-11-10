#pragma once

#include <stdint.h>

#if defined(_WIN32)
  #if defined(WII_BT_BUILD_DLL)
    #define WII_BT_API __declspec(dllexport)
  #else
    #define WII_BT_API __declspec(dllimport)
  #endif
  #define WII_BT_CALL __cdecl
#else
  #if defined(__GNUC__)
    #define WII_BT_API __attribute__((visibility("default")))
  #else
    #define WII_BT_API
  #endif
  #define WII_BT_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define WII_BT_API_VERSION 3u

WII_BT_API uint32_t WII_BT_CALL WiiBt_GetApiVersion(void);

enum WiiBtPairingMode {
    WII_BT_PAIRING_SYNC = 0,
    WII_BT_PAIRING_ONE_PLUS_TWO = 1
};

typedef struct WiiBtAdapterInfo {
    uint32_t struct_size;
    uint16_t vid;
    uint16_t pid;
    uint8_t bus;
    uint8_t address;
    uint8_t interface_number;
    uint8_t reserved;
    char manufacturer[128];
    char product[128];
} WiiBtAdapterInfo;

typedef struct WiiBtPlayerState {
    uint32_t struct_size;
    int32_t battery;
    uint16_t buttons;
    uint16_t pressed;
    uint8_t connected;
    uint8_t reserved;
    uint8_t tracking;
    uint8_t dots;
    double raw_x;
    double raw_y;
} WiiBtPlayerState;

typedef struct WiiBtSnapshot {
    uint32_t struct_size;
    uint32_t scanning;
    WiiBtPlayerState player[2];
    char message[512];
} WiiBtSnapshot;

/* Returns the number of claimable standard USB HCI adapters.
   Pass output=NULL/capacity=0 to query the count. */
WII_BT_API int32_t WII_BT_CALL WiiBt_EnumerateAdapters(
    WiiBtAdapterInfo* output, int32_t capacity);

/* Pass -1/-1 for automatic selection, or an explicit USB VID/PID pair. */
WII_BT_API int WII_BT_CALL WiiBt_HasCompatibleAdapter(int32_t vid, int32_t pid);

/* pairing_mode is WII_BT_PAIRING_SYNC or WII_BT_PAIRING_ONE_PLUS_TWO.
   key_file_utf8 may be NULL/empty to use the per-user WiiMoteDLL_Libusbk path.
   The instance owns its worker thread and libusb context. */
WII_BT_API void* WII_BT_CALL WiiBt_Create(
    int32_t vid, int32_t pid, int32_t pairing_mode, const char* key_file_utf8);
WII_BT_API void WII_BT_CALL WiiBt_Destroy(void* instance);
WII_BT_API int WII_BT_CALL WiiBt_Enable(void* instance, int enabled, int continuous);
WII_BT_API int WII_BT_CALL WiiBt_Scan(void* instance);
WII_BT_API int WII_BT_CALL WiiBt_Disconnect(void* instance, int player);
WII_BT_API int WII_BT_CALL WiiBt_SwapPlayers(void* instance);
WII_BT_API int WII_BT_CALL WiiBt_Poll(void* instance, WiiBtSnapshot* snapshot);

#ifdef __cplusplus
}
#endif
