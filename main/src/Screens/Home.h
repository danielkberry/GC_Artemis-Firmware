#ifndef CLOCKSTAR_FIRMWARE_HOME_H
#define CLOCKSTAR_FIRMWARE_HOME_H

#include <memory>
#include "LV_Interface/LVScreen.h"

/**
 * The screen shown on boot, on wake, and when leaving the main menu.
 * One place to change it: the watch is mounted on the inference box, so this is the
 * status face; the original clock lock screen is reachable as the "Clock" menu app.
 */
std::unique_ptr<LVScreen> makeHomeScreen();

#endif //CLOCKSTAR_FIRMWARE_HOME_H
