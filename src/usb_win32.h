#pragma once

#ifdef WIN32
#include <stdint.h>

void usb_win32_init();
void usb_win32_set_configuration(const char serial[], uint8_t configuration);
#endif