#ifndef _REPORTS_H_
#define _REPORTS_H_

#include <stdint.h>

// G29 HID report (Estructura final hacia la PS5)
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

// Driving Force HID report (Estructura base original de arranque)
typedef struct __attribute__((packed)) {
    uint32_t wheel : 10;
    uint32_t cross : 1;
    uint32_t square : 1;
    uint32_t circle : 1;
    uint32_t triangle : 1;
    uint32_t R1 : 1;
    uint32_t L1 : 1;
    uint32_t R2 : 1;
    uint32_t L2 : 1;
    uint32_t select : 1;
    uint32_t start : 1;
    uint32_t R3 : 1;
    uint32_t L3 : 1;
    uint32_t whatever : 2;
    uint8_t y;
    uint32_t hat : 4;
    uint32_t whatever2 : 4;
    uint8_t throttle;
    uint8_t brake;
} df_report_t;

// NUEVA: Estructura nativa del Logitech G25 (Modo 0xc299)
typedef struct __attribute__((packed)) {
    uint8_t wheel_low;
    uint8_t wheel_high : 6;
    uint8_t buttons1 : 2; // Botones del aro
    uint8_t buttons2;     // Botones del aro + Base
    uint8_t buttons3;     // Botones de la palanca (Marchas 1-6)
    uint8_t hat : 4;       // D-Pad de la palanca
    uint8_t buttons4 : 4; // Marcha atrás y otros
    uint8_t throttle;     // Acelerador (0-255)
    uint8_t brake;        // Freno (0-255)
    uint8_t clutch;       // Embrague (0-255)
} g25_native_report_t;

#endif
