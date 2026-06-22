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

uint8_t get_buffer[64];
uint8_t set_buffer[64];
uint8_t ff_buffer[64];

uint8_t report_[16]; 

void auth_task(void) {
    if (auth_device == 0 || !tuh_hid_is_ready(auth_device, auth_instance)) {
        return;
    }

    if (state == SENDING_RESET) {
        set_buffer[0] = 0x03;
        set_buffer[1] = 0x02;
        set_buffer[2] = 0x01;
        set_buffer[3] = 0x02;
        tuh_hid_set_report(auth_device, auth_instance, 0x03, HID_REPORT_TYPE_FEATURE, set_buffer, 48);
    }

    if (state == SENDING_NONCE) {
        memset(set_buffer, 0, 64);
        set_buffer[0] = 0xf0;
        set_buffer[1] = nonce_id;
        set_buffer[2] = nonce_part;
        set_buffer[3] = 0;
        memcpy(set_buffer + 4, nonce + (nonce_part * 56), 56);
        tuh_hid_set_report(auth_device, auth_instance, 0xf0, HID_REPORT_TYPE_FEATURE, set_buffer, 64);
    }

    if (state == RECEIVING_SIG) {
        memset(get_buffer, 0, 64);
        get_buffer[0] = 0xf1;
        get_buffer[1] = signature_part;
        tuh_hid_get_report(auth_device, auth_instance, 0xf1, HID_REPORT_TYPE_FEATURE, get_buffer, 64);
    }
}

void wheel_task(void) {
    if (wheel_device == 0 || !tuh_hid_is_ready(wheel_device, wheel_instance)) {
        return;
    }

    if (state == SENDING_NONCE || state == RECEIVING_SIG) {
        return;
    }

    if (busy) {
        return;
    }

    struct G29Report report;
    memset(&report, 0, sizeof(report));

    report.report_id = 1;

    if (wheel_pid == 0xC294) {
        // En caso de que se intente procesar datos en modo C294 antes de mutar, no hacemos nada.
        return;
    } 
    else if (wheel_pid == 0xC299) {
        // ========================================================
        //      MAPEO DE DATOS NATIVOS G25 (C299) -> PS4/PS5
        // ========================================================
        
        // 1. DIRECCIÓN (Steering) - 16 Bits, Byte 3 LSB y Byte 4 MSB
        report.steering = (report_[4] << 8) | report_[3];

        // 2. PEDALES (Invertidos por hardware Logitech: 255 = Suelto, 0 = Pisado a fondo)
        // Acelerador = Byte 5, Freno = Byte 6, Embrague = Byte 7
        report.gas    = 255 - report_[5];
        report.brake  = 255 - report_[6];
        report.clutch = 255 - report_[7];

        // 3. CRUCETA (D-PAD) - Parte baja del Byte 0
        report.dpad = report_[0] & 0x0F;

        // 4. BOTONES ROJOS DE LA PALANCA (4 botones encima de la cruceta)
        report.square   = (report_[0] & 0x10) ? 1 : 0; // Oeste
        report.cross    = (report_[0] & 0x20) ? 1 : 0; // Sur
        report.circle   = (report_[0] & 0x40) ? 1 : 0; // Este
        report.triangle = (report_[0] & 0x80) ? 1 : 0; // Norte

        // 5. LEVAS Y BOTONES DEL VOLANTE
        report.R1 = (report_[1] & 0x10) ? 1 : 0; // Leva Derecha
        report.L1 = (report_[1] & 0x20) ? 1 : 0; // Leva Izquierda
        report.R3 = (report_[1] & 0x40) ? 1 : 0; // Botón Rojo derecho del volante
        report.L3 = (report_[1] & 0x80) ? 1 : 0; // Botón Rojo izquierdo del volante

        // 6. BOTONES NEGROS DE LA PALANCA (4 botones debajo de la cruceta)
        report.share   = (report_[2] & 0x01) ? 1 : 0; // Botón negro 1 (Izquierda)
        report.options = (report_[2] & 0x02) ? 1 : 0; // Botón negro 2
        report.L2      = (report_[2] & 0x04) ? 1 : 0; // Botón negro 3
        report.R2      = (report_[2] & 0x08) ? 1 : 0; // Botón negro 4 (Derecha)

        // 7. BOTÓN PS VIRTUAL (COMBINACIÓN DE BOTONES)
        // G25 no tiene botón PS. Pulsando a la vez los 2 botones negros de la izquierda (Share + Options).
        if (report.share && report.options) {
            report.ps = 1;
            report.share = 0;
            report.options = 0;
        }

        // 8. MARCHAS (H-SHIFTER)
        // En el emulador del G29, las marchas se colocan como bits en la variable 'dummy1'
        report.dummy1 = 0;
        if (report_[2] & 0x10) report.dummy1 |= (1 << 0); // Marcha 1
        if (report_[2] & 0x20) report.dummy1 |= (1 << 1); // Marcha 2
        if (report_[2] & 0x40) report.dummy1 |= (1 << 2); // Marcha 3
        if (report_[2] & 0x80) report.dummy1 |= (1 << 3); // Marcha 4
        if (report_[1] & 0x01) report.dummy1 |= (1 << 4); // Marcha 5
        if (report_[1] & 0x02) report.dummy1 |= (1 << 5); // Marcha 6
        if (report_[1] & 0x04) report.dummy1 |= (1 << 6); // Reversa

    } else {
        // Fallback genérico para otros volantes si no es un G25 en modo Nativo
        report.steering = (report_[1] << 8) | report_[0];
        report.gas      = 255 - report_[2];
        report.brake    = 255 - report_[3];
        report.clutch   = 255 - report_[4];
    }

    tud_hid_report(1, &report, sizeof(report));

    busy = true;
    tuh_hid_receive_report(wheel_device, wheel_instance);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    if (state == WAITING_FOR_SIG) {
        state = RECEIVING_SIG;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    if (report_id == 0xf3) {
        buffer[0] = 0xf3;
        buffer[1] = 0x00;
        buffer[2] = 0x38;
        buffer[3] = 0x38;
        buffer[4] = 0x00;
        buffer[5] = 0x00;
        buffer[6] = 0x00;
        buffer[7] = 0x00;
        return 8;
    }
    
    if (report_id == 0xf1) {
        buffer[0] = 0xf1;
        buffer[1] = signature_part;
        buffer[2] = 0;
        buffer[3] = 0;
        memcpy(buffer + 4, signature + (signature_part * 56), 56);
        
        signature_part++;
        if (signature_part == 19) {
            signature_part = 0;
            signature_ready = 0;
            board_led_write(1);
        }
        return 64;
    }

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (report_id == 0xf0) {
        nonce_id = buffer[1];
        nonce_part = buffer[2];
        memcpy(nonce + (nonce_part * 56), buffer + 4, 56);
        if (nonce_part == 4) {
            nonce_ready = 1;
            board_led_write(0);
        }
    }

    if (wheel_device != 0) {
        // Envia los comandos de FFB (Force Feedback) generados por el juego al volante.
        tuh_hid_send_report(wheel_device, wheel_instance, report_id, buffer, bufsize);
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if (dev_addr == wheel_device && instance == wheel_instance) {
        memcpy(report_, report, len);
        
        // ¡CUIDADO! Imprimir cada evento del volante causa mucha latencia USB.
        // Debe estar comentado para uso normal para evitar desconexiones en la consola.
        /*
        printf("[DATA C299] ");
        for(uint8_t i = 0; i < len; i++) {
            printf("%02X ", report_[i]);
        }
        printf("\r\n");
        */
        
        busy = false;
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    if (vid == 0x0f0d && pid == 0x012c) {
        printf("[AUTH] Mando original Hori asignado.\r\n");
        auth_device = dev_addr;
        auth_instance = instance;
    }
    
    if (vid == 0x046d && pid == 0xc294) {
        printf("[WHEEL] Volante G25 asignado.\r\n");
        wheel_device = dev_addr;
        wheel_instance = instance;
        wheel_pid = pid;

        printf("[WHEEL] Estado C294 -> Forzando mutacion estricta a G25 Nativo (0xF8 0x10)...\r\n");
        // Envia el Reporte mágico de logitech que activa el modo G25 Nativo
        uint8_t init_buf[7] = { 0x00, 0x02, 0x00, 0x7F, 0x88, 0xFF, 0xFF };
        tuh_hid_send_report(wheel_device, wheel_instance, 0, init_buf, sizeof(init_buf));
        return;
    }
    
    if (vid == 0x046d && pid == 0xc299) {
        printf("\r\n========================================================\r\n");
        printf(" [OK] ¡VOLANTE EN MODO NATIVO G25 (C299) CONFIGURADO!\r\n");
        printf("========================================================\r\n\r\n");
        wheel_device = dev_addr;
        wheel_instance = instance;
        wheel_pid = pid;
        tuh_hid_receive_report(wheel_device, wheel_instance);
        return;
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr == wheel_device && instance == wheel_instance) {
        printf("[DESCONEXION] Volante G25 desconectado.\r\n");
        wheel_device = 0;
        wheel_instance = 0;
        wheel_pid = 0;
    }
    if (dev_addr == auth_device && instance == auth_instance) {
        printf("[DESCONEXION] Mando de autenticacion desconectado.\r\n");
        auth_device = 0;
        auth_instance = 0;
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device && instance == auth_instance) {
        if (state == SENDING_RESET) {
            state = IDLE;
        }
        if (state == SENDING_NONCE) {
            nonce_part++;
            if (nonce_part == 5) {
                state = WAITING_FOR_SIG;
                expected_part = 0;
            }
        }
    }
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device && instance == auth_instance) {
        if (state == RECEIVING_SIG) {
            if (get_buffer[1] == expected_part) {
                memcpy(signature + (expected_part * 56), get_buffer + 4, 56);
                expected_part++;
                if (expected_part == 19) {
                    state = IDLE;
                    signature_ready = 1;
                } else {
                    signature_part = expected_part;
                }
            } else {
                signature_part = expected_part;
            }
        }
    }
}

int main(void) {
    board_init();
    stdio_init_all();
    
    printf("\r\n==================================================\r\n");
    printf("        SISTEMA DE TRADUCCIÓN G25 NATIVO (C299)     \r\n");
    printf("==================================================\r\n\r\n");

    tusb_init();

    while (1) {
        tud_task();
        tuh_task();
        auth_task();
        wheel_task();

        if (nonce_ready) {
            state = SENDING_NONCE;
            nonce_part = 0;
            nonce_ready = 0;
        }

        if (initialized) {
            initialized = false;
            state = SENDING_RESET;
        }
    }

    return 0;
}
