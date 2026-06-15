#ifndef _REPORTS_H_
#define _REPORTS_H_

#include <stdint.h>

// G29 HID report (Lo que va a la PS5)
typedef struct __attribute__((packed)) {
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint32_t dpad : 4;
    uint32_t square : 1;
    uint32_t cross : 1;
    uint32_t circle : 1;
    uint32_t triangle : 1;
    uint32_t L1 : 1;
    uint32_t R1 : 1;
    uint32_t L2 : 1;
    uint32_t R2 : 1;
    uint32_t select : 1;
    uint32_t start : 1;
    uint32_t L3 : 1;
    uint32_t R3 : 1;
    uint32_t PS : 1;
    uint32_t touchpad : 1;
    uint32_t counter : 6;
    uint8_t whatever[35];
    uint16_t wheel;
    uint16_t throttle;
    uint16_t brake;
    uint16_t clutch;
    uint8_t whatever2[13];
} g29_report_t;

// Estructura híbrida para el G25 (Modo extendido real)
typedef struct __attribute__((packed)) {
    uint16_t wheel : 14;
    uint32_t buttons : 18;
    uint8_t throttle;
    uint8_t brake;
    uint8_t clutch;
    uint8_t dpad;
} g25_report_t;

#endif
