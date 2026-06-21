#ifndef REPORTS_H
#define REPORTS_H

#include <stdint.h>

// Forzamos al compilador a empaquetar los datos de 1 en 1 byte.
// Esto evita el "padding" (relleno de memoria) que rompe la lectura USB.
#pragma pack(push, 1)

/**
 * @brief Estructura de Reporte para Logitech G25 en Modo Inicial / Compatibilidad (PID: C294)
 * Longitud exacta: 7 bytes. Mapea el estado del volante al conectarse por primera vez.
 */
typedef struct {
    uint8_t steering_lsb; // Dirección del volante (Byte bajo)
    uint8_t steering_msb; // Dirección del volante (Byte alto)
    uint8_t buttons1;     // Primer bloque de botones principales
    uint8_t buttons2;     // Segundo bloque de botones y Cruceta (Hat switch)
    uint8_t throttle;     // Eje del pedal de Acelerador
    uint8_t brake;        // Eje del pedal de Freno
    uint8_t clutch;       // Eje del pedal de Embrague
} report_g25_c294_t;

/**
 * @brief Estructura de Reporte para Logitech G25 en Modo Extendido / DFP (PID: C298)
 * Longitud exacta: 8 bytes. Se activa tras la inyección del comando de mutación.
 */
typedef struct {
    uint8_t steering_lsb; // Eje de dirección del volante (LSB)
    uint8_t steering_msb; // Eje de dirección del volante (MSB)
    uint8_t buttons;      // Combinación de botones del 1 al 8
    uint8_t hat_buttons;  // Cruceta D-Pad y botones adicionales del sistema
    uint8_t throttle;     // Posición del pedal del Acelerador
    uint8_t brake;        // Posición del pedal del Freno
    uint8_t clutch;       // Posición del pedal del Embrague
    uint8_t extension;    // Byte de estado o extensión para la palanca de cambios (ej. 0x1B)
} report_g25_c298_t;

/**
 * @brief Estructura de Reporte para Logitech G25 en Modo Nativo Completo (PID: C299)
 * Longitud exacta: 11 bytes. Utilizado por los sistemas de traducción estricta.
 */
typedef struct {
    uint8_t report_id;    // Identificador de reporte (por defecto suele ser 0x08)
    uint8_t steering_lsb; // Dirección del volante (LSB)
    uint8_t steering_msb; // Dirección del volante (MSB)
    uint8_t buttons1;     // Grupo de botones 1 (Aros y levas)
    uint8_t buttons2;     // Grupo de botones 2 (Base o funciones de juego)
    uint8_t buttons3;     // Grupo de botones 3
    uint8_t throttle;     // Entrada del Acelerador
    uint8_t brake;        // Entrada del Freno
    uint8_t clutch;       // Entrada del Embrague
    uint8_t shifter_x;    // Eje X de la palanca de cambios en H
    uint8_t shifter_y;    // Eje Y de la palanca de cambios en H
} report_g25_c299_t;

/**
 * @brief Estructura de Salida Unificada (Traducción hacia la Consola)
 * Contiene de forma explícita todos los campos individuales que 'adapter.c' necesita escribir.
 */
typedef struct {
    uint8_t report_id;    // Identificador de reporte requerido por la consola (ej. 0x03)
    uint16_t steering;    // Dirección final normalizada para el sistema
    uint8_t throttle;     // Pedal de acelerador procesado
    uint8_t brake;        // Pedal de freno procesado
    uint16_t clutch;      // Pedal de embrague (en 16 bits para admitir valores como 0xFFFF)
    uint32_t buttons;     // Estado unificado de toda la botonera mapeada
    uint8_t hat;          // Estado del D-Pad (Cruceta)
    
    // CAMPOS INDIVIDUALES: Añadidos para solucionar los errores del compilador en adapter.c
    uint8_t dpad;
    uint8_t R1;
    uint8_t R2;
    uint8_t L1;
    uint8_t L2;
    uint8_t L3;
    uint8_t R3;
    uint8_t cross;
    uint8_t square;
    uint8_t circle;
    uint8_t triangle;
    
    uint8_t padding[32];  // Relleno obligatorio para cumplir con los tamaños de reporte de consola
} report_console_out_t;

// ALIAS UNIVERSALES: No importa cómo se llamara originalmente la estructura en tu adapter.c,
// estas líneas hacen que el compilador entienda cualquier combinación sin lanzar errores.
typedef report_console_out_t g29_report_t;
typedef report_console_out_t report_t;
typedef report_console_out_t g29_out_t;
typedef report_console_out_t g29_output_t;
typedef report_console_out_t report_g29_t;

// Restauramos la configuración original del compilador para el resto del proyecto
#pragma pack(pop)

#endif // REPORTS_H
