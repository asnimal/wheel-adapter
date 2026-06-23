#ifndef _REPORTS_H_
#define _REPORTS_H_

#include <stdint.h>

// Reporte HID de G29 (Modificado con la estructura oficial de 20 botones para PS5)
typedef struct __attribute__((packed)) {
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    
    // Bloque de 24 bits (3 bytes de datos para cruceta y botones)
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
    
    // Mapeo físico nativo de las marchas (Botones 13 al 19) y botón PS (Botón 20)
    uint32_t gear_1 : 1;  // Botón 13
    uint32_t gear_2 : 1;  // Botón 14
    uint32_t gear_3 : 1;  // Botón 15
    uint32_t gear_4 : 1;  // Botón 16
    uint32_t gear_5 : 1;  // Botón 17
    uint32_t gear_6 : 1;  // Botón 18
    uint32_t reverse : 1; // Botón 19
    uint32_t PS : 1;       // Botón 20
    
    uint8_t whatever[35]; // Se mantiene en 35 bytes para una alineación exacta de la memoria
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
