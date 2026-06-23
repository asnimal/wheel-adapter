#ifndef _REPORTS_H_
#define _REPORTS_H_

#include <stdint.h>

// Reporte HID de G29 (Modificado con soporte para 25 botones)
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
    
    // Mapeo de Marchas en H (Botones 14 al 20)
    uint32_t gear_1 : 1;  // Marcha 1
    uint32_t gear_2 : 1;  // Marcha 2
    uint32_t gear_3 : 1;  // Marcha 3
    uint32_t gear_4 : 1;  // Marcha 4
    uint32_t gear_5 : 1;  // Marcha 5
    uint32_t gear_6 : 1;  // Marcha 6
    uint32_t reverse : 1; // Marcha Atrás
    
    // Botones adicionales para cumplir con el descriptor de 25 botones (Botones 21 al 24)
    uint32_t btn_21 : 1;
    uint32_t btn_22 : 1;
    uint32_t btn_23 : 1;
    uint32_t btn_24 : 1;
    
    uint32_t counter : 3; // 3 bits de relleno para completar los 32 bits (4 bytes)
    uint8_t whatever[34]; // Reducido de 35 a 34 para mantener la alineación exacta
    uint16_t wheel;
    uint16_t throttle;
    uint16_t brake;
    uint16_t clutch;
    uint8_t whatever2[13];
} g29_report_t;

// Reporte HID de Driving Force
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

#endif
