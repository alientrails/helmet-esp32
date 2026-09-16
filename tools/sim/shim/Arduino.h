#pragma once
// Minimal Arduino shim so the firmware's headers compile on the host for the
// preview build. Only the types leds.h/config.h actually need.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis(void);
#ifdef __cplusplus
}
#endif
