#ifndef REPORTS_H_
#define REPORTS_H_

#include <stdint.h>

// Estructura de reporte completa para el volante emulado G29
typedef struct __attribute__((packed)) {
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;

    // Botones principales (Byte 4)
    uint8_t dpad     : 4;
    uint8_t square   : 1;
    uint8_t cross    : 1;
    uint8_t circle   : 1;
    uint8_t triangle : 1;

    // Botones secundarios (Byte 5)
    uint8_t L1       : 1;
    uint8_t R1       : 1;
    uint8_t L2       : 1;
    uint8_t R2       : 1;
    uint8_t select   : 1;
    uint8_t start    : 1;
    uint8_t L3       : 1;
    uint8_t R3       : 1;

    // Botón PS y Marchas de la palanca en H (Byte 6 y Byte 7)
    uint8_t PS       : 1;
    uint8_t tpad     : 1; 
    uint8_t gear1    : 1;
    uint8_t gear2    : 1;
    uint8_t gear3    : 1;
    uint8_t gear4    : 1;
    uint8_t gear5    : 1;
    uint8_t gear6    : 1;

    uint8_t reverse  : 1;
    uint8_t padding1 : 7;

    // Ejes de control de conducción (16 bits)
    uint16_t wheel;     
    uint16_t throttle;  
    uint16_t brake;     
    uint16_t clutch;    

    // Relleno final para el tamaño de reporte estándar de simulación
    uint8_t padding2[30]; 
} g29_report_t;

// Estructura para interpretar el reporte del Volante DFGT (C294)
typedef struct __attribute__((packed)) {
    uint16_t wheel;
    uint8_t throttle;
    uint8_t brake;
    uint8_t hat;
    uint8_t cross;
    uint8_t square;
    uint8_t circle;
    uint8_t triangle;
    uint8_t L2;
    uint8_t L1;
    uint8_t R2;
    uint8_t R1;
    uint8_t L3;
    uint8_t select;
    uint8_t start;
    uint8_t R3;
} df_report_t;

#endif /* REPORTS_H_ */
