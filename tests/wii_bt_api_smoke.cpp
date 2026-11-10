#include "wii_bt_api.h"

int main() {
    if (WiiBt_GetApiVersion() != WII_BT_API_VERSION)
        return 1;
    if (WiiBt_Create(-2, -2) != nullptr)
        return 2;
    if (WiiBt_Create(0x0A12, -1) != nullptr)
        return 3;
    if (WiiBt_HasCompatibleAdapter(-2, -2) != 0)
        return 4;
    return 0;
}
