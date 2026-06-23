#ifndef _REPORTS_H_
#define _REPORTS_H_

#include <stdint.h>

// Reporte HID de G29 (Modificado con la estructura oficial de 21 botones para PS5)
typedef struct __attribute__((packed)) {
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    
    // Bloque de botones y D-PAD oficial (Alineado estrictamente a 32 bits / 4 bytes)
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
    uint32_t PS : 1;         // Botón 13 (Estándar de PS)
    uint32_t touchpad : 1;   // Botón 14 (Estándar de PS)
    
    // Mapeo oficial de Marchas G29 en H (Botones 15 al 21)
    uint32_t gear_1 : 1;     // Botón 15 (Marcha 1)
    uint32_t gear_2 : 1;     // Botón 16 (Marcha 2)
    uint32_t gear_3 : 1;     // Botón 17 (Marcha 3)
    uint32_t gear_4 : 1;     // Botón 18 (Marcha 4)
    uint32_t gear_5 : 1;     // Botón 19 (Marcha 5)
    uint32_t gear_6 : 1;     // Botón 20 (Marcha 6)
    uint32_t reverse : 1;    // Botón 21 (Marcha Atrás)
    
    uint32_t counter : 7;    // 7 bits de relleno para completar exactamente 32 bits (4 bytes)
    uint8_t whatever[34];    // Reducido de 35 a 34 para mantener la alineación estructural exacta
    uint16_t wheel;
    uint16_t throttle;
    uint16_t brake;
    uint16_t clutch;
    uint8_t whatever2[13];
} g29_report_t;

// Driving Force HID report
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
