// board/board.cpp
#include "board.h"
#include <esp_system.h>

namespace Board {

const char* name() {
    return BOARD_NAME;
}

const char* chip() {
    const char* m = ESP.getChipModel();
    return m ? m : "ESP32";
}

uint8_t defaultButtonGpio() {
    return (uint8_t)BUTTON_GPIO_DEFAULT;
}

bool gpioAllowed(int gpio) {
    if (gpio < 0) return false;
#if CONFIG_IDF_TARGET_ESP32C3
    return gpio <= 21;
#elif CONFIG_IDF_TARGET_ESP32C6
    return gpio <= 30;
#elif CONFIG_IDF_TARGET_ESP32S3
    return gpio <= 48;
#elif CONFIG_IDF_TARGET_ESP32S2
    return gpio <= 46;
#else
    if (gpio >= 6 && gpio <= 11) return false;
    return gpio <= 39;
#endif
}

uint32_t flashBytes() {
    return (uint32_t)ESP.getFlashChipSize();
}

} // namespace Board
