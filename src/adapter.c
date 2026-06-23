#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "pico/stdio.h"

#include "reports.h"

uint8_t nonce_id;
uint8_t nonce[280];
uint8_t nonce_part = 0;
uint8_t signature[1064];
uint8_t signature_part = 0;
uint8_t signature_ready = 0;
uint8_t nonce_ready = 0;

uint8_t expected_part = 0;

uint8_t wheel_device = 0;
uint8_t wheel_instance = 0; 
uint16_t wheel_pid = 0;       
uint8_t auth_device = 0;
uint8_t auth_instance = 0;

bool busy = false;

enum {
    IDLE = 0,
    SENDING_RESET = 1,
    SENDING_NONCE = 2,
    WAITING_FOR_SIG = 3,
    RECEIVING_SIG = 4,
};

uint8_t state = IDLE;

bool initialized = true;
bool calibration_done = false; // Bloqueador de paquetes FFB mientras calibra

uint8_t get_buffer[64];
uint8_t set_buffer[64];
uint8_t ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t prev_ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

g29_report_t report;
g29_report_t prev_report;

// G29 Descriptor de simulación para PS5
const uint8_t output_0x03[] = {
    0x21, 0x27, 0x03, 0x11, 0x06, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0D, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x0D, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const uint8_t output_0xf3[] = { 0x0, 0x38, 0x38, 0, 0, 0, 0 };

void report_init() {
    memset(&report, 0, sizeof(report));
    report.lx = 0x80;
    report.ly = 0x80;
    report.rx = 0x80;
    report.ry = 0x80;
    report.clutch = 0xFFFF;
    memcpy(&prev_report, &report, sizeof(report));
}

void hid_task() {
    if (!tud_hid_ready()) {
        return;
    }

    // Combinación física real para el botón PS (L3 + R3)
    report.PS = report.L3 && report.R3;
    if (memcmp(&prev_report, &report, sizeof(report))) {
        tud_hid_report(1, &report, sizeof(report));
        memcpy(&prev_report, &report, sizeof(report));
    }

    // Solo reenviar paquetes de vibración del juego si el volante ha terminado su calibración
    if (memcmp(prev_ff_buf, ff_buf, sizeof(ff_buf))) {
        if (wheel_device && calibration_done) {
            tuh_hid_send_report(wheel_device, wheel_instance, 0, ff_buf, sizeof(ff_buf));
        }
        memcpy(prev_ff_buf, ff_buf, sizeof(ff_buf));
    }
}

void wheel_init_task() {
    static uint32_t c299_mount_time = 0;
    uint32_t current_time = board_millis();

    if (wheel_device) {
        if (wheel_pid == 0xc294) {
            calibration_done = false;
            // Mutación instantánea (0ms) para evitar que empiece a calibrarse en el modo DF (C294)
            static uint8_t cmd_g25_native[] = { 0xf8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00 };
            printf("[WHEEL] Estado C294 -> Forzando mutacion estricta a G25 Nativo (0xF8 0x10) de forma inmediata...\n");
            tuh_hid_set_report(wheel_device, wheel_instance, 0, HID_REPORT_TYPE_OUTPUT, cmd_g25_native, sizeof(cmd_g25_native));
            wheel_pid = 0; // Evita re-envíos
        } 
        else if (wheel_pid == 0xc299) {
            if (!initialized) {
                initialized = true;
                calibration_done = false; // Bloquea FFB de la consola mientras calibra
                c299_mount_time = current_time;
                printf("\n========================================================\n");
                printf(" [OK] VOLANTE G25 DETECTADO EN MODO NATIVO (C299)\n");
                printf(" Iniciando auto-calibrado físico continuo. Esperando 6s...\n");
                printf("========================================================\n\n");
            }

            // Esperar 6 segundos para dar tiempo a que termine de girar y centrarse por completo
            if (!calibration_done && (current_time - c299_mount_time >= 6000)) {
                calibration_done = true; // Desbloquea los efectos de fuerza de los juegos
                printf("[WHEEL] Calibrado completado sin interrupciones. Motores desbloqueados.\n");
                static uint8_t buf[] = { 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
                tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
                tuh_hid_set_report(wheel_device, wheel_instance, 0, HID_REPORT_TYPE_OUTPUT, buf, sizeof(buf));
            }
        }
    }
}

void auth_task() {
    if (!busy && auth_device) {
        switch (state) {
            case IDLE:
                break;
            case SENDING_RESET:
                tuh_hid_get_report(auth_device, auth_instance, 0xF3, HID_REPORT_TYPE_FEATURE, get_buffer, 7 + 1);
                busy = true;
                break;
            case SENDING_NONCE:
                set_buffer[0] = 0xF0;
                set_buffer[1] = nonce_id;
                set_buffer[2] = nonce_part;
                set_buffer[3] = 0;
                memcpy(set_buffer + 4, nonce + (nonce_part * 56), 56);
                tuh_hid_set_report(auth_device, auth_instance, 0xF0, HID_REPORT_TYPE_FEATURE, set_buffer, 64);
                busy = true;
                nonce_part++;
                break;
            case WAITING_FOR_SIG:
                tuh_hid_get_report(auth_device, auth_instance, 0xF2, HID_REPORT_TYPE_FEATURE, get_buffer, 15 + 1);
                busy = true;
                break;
            case RECEIVING_SIG:
                tuh_hid_get_report(auth_device, auth_instance, 0xF1, HID_REPORT_TYPE_FEATURE, get_buffer, 63 + 1);
                busy = true;
                break;
        }
    }
}

int main() {
    board_init();
    report_init();
    tusb_init();
    stdio_init_all();

    printf("\n==================================================\n");
    printf("        SISTEMA DE TRADUCCIÓN G25 NATIVO (C299)     \n");
    printf("==================================================\n");
    while (1) {
        tuh_task();
        tud_task();
        hid_task();
        auth_task();
        wheel_init_task();
    }

    return 0;
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device) {
        busy = false;
        switch (report_id) {
            case 0xF3:
                state = SENDING_NONCE;
                break;
            case 0xF2:
                if (get_buffer[2] == 0) {
                    signature_part = 0;
                    state = RECEIVING_SIG;
                }
                break;
            case 0xF1:
                memcpy(signature + (signature_part * 56), get_buffer + 4, 56);
                signature_part++;
                if (signature_part == 19) {
                    state = IDLE;
                    expected_part = 0;
                    signature_ready = true;
                    signature_part = 0;
                    printf("[AUTH] Firma completa descargada.\n");
                }
                break;
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ((dev_addr == auth_device) && (report_id == 0xF0)) {
        busy = false;
        if (nonce_part == 5) {
            state = WAITING_FOR_SIG;
        }
    }
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    switch (report_id) {
        case 0x03:
            memcpy(buffer, output_0x03, reqlen);
            board_led_write(false);
            return reqlen;
        case 0xF3:
            memcpy(buffer, output_0xf3, reqlen);
            signature_ready = false;
            return reqlen;
        case 0xF1: {  
            buffer[0] = nonce_id;
            buffer[1] = signature_part;
            buffer[2] = 0;
            memcpy(&buffer[3], &signature[signature_part * 56], 56);
            signature_part++;
            if (signature_part == 19) {
                signature_part = 0;
                board_led_write(true);
            }
            return reqlen;
        }
        case 0xF2: {  
            buffer[0] = nonce_id;
            buffer[1] = signature_ready ? 0 : 16;
            memset(&buffer[2], 0, 9);
            return reqlen;
        }
    }
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (report_id == 0xF0) {  
        uint8_t part = expected_part;
        if (bufsize == 63) {
            nonce_id = buffer[0];
            part = buffer[1];
        }
        if (part > 4) {
            return;
        }
        expected_part = part + 1;
        memcpy(&nonce[part * 56], &buffer[3], 56);
        if (part == 4) {
            nonce_ready = 1;
            state = SENDING_RESET;
            nonce_part = 0;
        }
    } else {
        if (bufsize > sizeof(ff_buf)) {
            memcpy(ff_buf, buffer + 1, sizeof(ff_buf));
        }
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    printf("\n[CONEXIÓN] USB Host -> VID:%04X PID:%04X\n", vid, pid);
    if ((vid == 0x046d) && ((pid == 0xc294) || (pid == 0xc299))) {  
        wheel_device = dev_addr;
        wheel_instance = instance; 
        wheel_pid = pid;
        tuh_hid_receive_report(dev_addr, instance);
        initialized = false;
        calibration_done = false;
        printf("[WHEEL] Volante G25 asignado.\n");
    } else {  
        auth_device = dev_addr;
        auth_instance = instance;
        printf("[AUTH] Mando original asignado.\n");
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("\n[DESCONEXIÓN] Dispositivo retirado -> addr: %d\n", dev_addr);
    if (dev_addr == wheel_device) {
        wheel_device = 0;
        wheel_instance = 0;
        wheel_pid = 0;
        calibration_done = false;
    }
    if (dev_addr == auth_device) {
        auth_device = 0;
        auth_instance = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    static uint32_t last_print_time = 0;
    uint32_t now = board_millis();

    if (len > 0 && dev_addr == wheel_device) {
        
        // LIMITADOR DE LOG: Evita que el timestamp inunde Putty (Imprime cada 300ms)
        if (now - last_print_time >= 300) {
            printf("[DATA G25] ");
            for (uint16_t i = 0; i < len; i++) printf("%02X ", report_[i]);
            printf("\n");
            last_print_time = now;
        }

        if (wheel_pid == 0xc294) {
            df_report_t* df = (df_report_t*) report_;
            report.wheel = df->wheel << 6;
            report.throttle = df->throttle << 8;
            report.brake = df->brake << 8;
            report.clutch = 0xFFFF;
            report.dpad = df->hat;
            report.cross = df->cross;
            report.square = df->square;
            report.circle = df->circle;
            report.triangle = df->triangle;
            report.L2 = df->L2;
            report.L1 = df->L1;
            report.R2 = df->R2;
            report.R1 = df->R1;
            report.select   = df->L3;
            report.L3       = df->select; 
            report.R3       = df->start;
            report.start    = df->R3;     
        } 
        else if (wheel_pid == 0xc299) {
            // =================================================================
            // DETECCIÓN Y MAPEO EN MODO NATIVO G25 (C299) -> TRADUCCIÓN A G29
            // =================================================================

            // 1. DIRECCIÓN
            uint16_t raw_steering = report_[3] | (report_[4] << 8);
            report.wheel = raw_steering;

            // 2. PEDALES DE CARRERA (0xFF en reposo, 0x00 pisado)
            report.throttle = report_[5] << 8;
            report.brake    = report_[6] << 8;

            // Embrague: filtrado de oscilaciones parásitas
            if (report_[7] >= 0xF5) {
                report.clutch = 0xFF00; // En reposo (suelto)
            } else {
                report.clutch = report_[7] << 8; // Escalado progresivo de pisada
            }

            // 3. CRUCETA (D-PAD)
            uint8_t hat = report_[0] & 0x0F;
            report.dpad = (hat <= 7) ? hat : 0x08;

            // 4. BOTONES DEL PANEL DE LA PALANCA (Los 4 negros)
            report.cross    = (report_[0] & 0x10) ? 1 : 0; 
            report.square   = (report_[0] & 0x20) ? 1 : 0; 
            report.circle   = (report_[0] & 0x40) ? 1 : 0; 
            report.triangle = (report_[0] & 0x80) ? 1 : 0; 

            // 5. BOTONES Y LEVAS DEL VOLANTE
            report.R1       = (report_[1] & 0x01) ? 1 : 0; // Leva Izquierda física actúa como R1
            report.L1       = (report_[1] & 0x02) ? 1 : 0; // Leva Derecha física actúa como L1
            report.R2       = (report_[1] & 0x04) ? 1 : 0; // Botón Izquierdo físico actúa como R2
            report.L2       = (report_[1] & 0x08) ? 1 : 0; // Botón Derecho físico actúa como L2

            // 6. BOTONES ROJOS (Orden físico de izquierda a derecha mapeado a: select, L3, R3, start)
            report.select   = (report_[1] & 0x80) ? 1 : 0; // Botón rojo 1 (Izquierdo, 0x80) -> SELECT
            report.L3       = (report_[1] & 0x10) ? 1 : 0; // Botón rojo 2 (Central Izquierdo, 0x10) -> L3
            report.R3       = (report_[1] & 0x20) ? 1 : 0; // Botón rojo 3 (Central Derecho, 0x20) -> R3
            report.start    = (report_[1] & 0x40) ? 1 : 0; // Botón rojo 4 (Derecho, 0x40) -> START

            // 7. PALANCA DE CAMBIOS EN PATRÓN H (Botones estándar 15 al 21 en G29)
            uint8_t gears = report_[2];
            report.gear_1  = (gears & 0x01) ? 1 : 0; // Marcha 1
            report.gear_2  = (gears & 0x02) ? 1 : 0; // Marcha 2
            report.gear_3  = (gears & 0x04) ? 1 : 0; // Marcha 3
            report.gear_4  = (gears & 0x08) ? 1 : 0; // Marcha 4
            report.gear_5  = (gears & 0x10) ? 1 : 0; // Marcha 5
            report.gear_6  = (gears & 0x20) ? 1 : 0; // Marcha 6
            report.reverse = (gears & 0x40) ? 1 : 0; // Marcha Atrás
        }
    }

    tuh_hid_receive_report(dev_addr, instance);
}
