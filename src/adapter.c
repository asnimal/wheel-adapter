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
uint16_t wheel_pid = 0; // NUEVO: Para almacenar el PID actual del volante
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
uint8_t ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t prev_ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

g29_report_t report;
g29_report_t prev_report;

// G29
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

    report.PS = report.L3 && report.R3;

    if (memcmp(&prev_report, &report, sizeof(report))) {
        tud_hid_report(1, &report, sizeof(report));
        memcpy(&prev_report, &report, sizeof(report));
    }

    if (memcmp(prev_ff_buf, ff_buf, sizeof(ff_buf))) {
        if (wheel_device) {
            tuh_hid_send_report(wheel_device, wheel_instance, 0, ff_buf, sizeof(ff_buf));
        }
        memcpy(prev_ff_buf, ff_buf, sizeof(ff_buf));
    }
}

void wheel_init_task() {
    if (wheel_device && !initialized) {
        initialized = true;
        
        if (wheel_pid == 0xc294) {
            // El volante acaba de conectarse en modo antiguo.
            // Forzamos el reinicio a modo G25 Nativo (0xc299).
            static uint8_t buf[] = { 0xf8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00 };
            tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
        } else if (wheel_pid == 0xc299) {
            // El volante ya está en modo nativo 100%. Desactivamos su muelle interno por si acaso.
            static uint8_t buf[] = { 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
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
                printf(".");
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
                printf("Sending nonce to auth controller");
                state = SENDING_NONCE;
                break;
            case 0xF2:
                if (get_buffer[2] == 0) {
                    signature_part = 0;
                    state = RECEIVING_SIG;
                    printf("\n");
                    printf("Receiving signature from auth controller");
                }
                break;
            case 0xF1:
                memcpy(signature + (signature_part * 56), get_buffer + 4, 56);
                signature_part++;
                printf(".");
                if (signature_part == 19) {
                    state = IDLE;
                    expected_part = 0;
                    signature_ready = true;
                    signature_part = 0;
                    printf("\n");
                }
                break;
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ((dev_addr == auth_device) && (report_id == 0xF0)) {
        busy = false;
        if (nonce_part == 5) {
            printf("\n");
            printf("Waiting for auth controller to sign...\n");
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
            if (signature_part == 0) {
                printf("Sending signature to PS5");
            }
            printf(".");
            memcpy(&buffer[3], &signature[signature_part * 56], 56);
            signature_part++;
            if (signature_part == 19) {
                signature_part = 0;
                printf("\n");
                board_led_write(true);
            }
            return reqlen;
        }
        case 0xF2: {  
            printf("PS5 asks if signature ready (%s).\n", signature_ready ? "yes" : "no");
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
        if (part == 0) {
            printf("Getting nonce from PS5");
        }
        printf(".");
        if (part > 4) {
            return;
        }
        expected_part = part + 1;
        memcpy(&nonce[part * 56], &buffer[3], 56);
        if (part == 4) {
            nonce_ready = 1;
            printf("\n");
            printf("Sending reset to auth controller...\n");
            state = SENDING_RESET;
            nonce_part = 0;
        }
    } else {
        if (bufsize > sizeof(ff_buf)) {
            // Mantenemos el filtro que bloquea las órdenes de endurecer artificialmente el volante
            uint8_t cmd = buffer[1];
            if (cmd == 0x12 || cmd == 0xf5) {
                return;
            }
            memcpy(ff_buf, buffer + 1, sizeof(ff_buf));
        }
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    printf("tuh_hid_mount_cb %04x:%04x %d %d\n", vid, pid, dev_addr, instance);

    // NUEVO: Aceptamos ambos, el modo antiguo (0xc294) y el modo G25 Nativo (0xc299)
    if ((vid == 0x046d) && ((pid == 0xc294) || (pid == 0xc299))) {  
        wheel_device = dev_addr;
        wheel_instance = instance;
        wheel_pid = pid; // Guardamos en qué modo está conectado
        tuh_hid_receive_report(dev_addr, instance);
        initialized = false;
    } else {  
        auth_device = dev_addr;
        auth_instance = instance;
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("tuh_hid_umount_cb\n");
    if (dev_addr == wheel_device) {
        wheel_device = 0;
        wheel_instance = 0;
        wheel_pid = 0;
    }
    if (dev_addr == auth_device) {
        auth_device = 0;
        auth_instance = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (len > 0) {
        if (dev_addr == wheel_device) {
            
            // PROCESAMOS LOS DATOS DEPENDIENDO DEL MODO EN EL QUE ESTÉ EL VOLANTE
            if (wheel_pid == 0xc299) {
                // MODO G25 NATIVO
                g25_report_t* g25 = (g25_report_t*) report_;
                
                // El G25 reporta el giro en 14 bits, la PS5/G29 lo espera adaptado
                report.wheel = g25->wheel << 2;
                
                // En modo nativo, el G25 devuelve los pedales invertidos (255 suelto, 0 pisado)
                // Adaptamos las escalas y las invertimos correctamente para la consola
                report.throttle = (255 - g25->throttle) << 8;
                report.brake    = (255 - g25->brake) << 8;
                
                // El embrague en la PS5 se espera en 0xFFFF por defecto cuando está suelto
                report.clutch   = g25->clutch << 8 | g25->clutch; 

                report.dpad     = g25->hat;
                report.cross    = g25->cross;
                report.square   = g25->square;
                report.circle   = g25->circle;
                report.triangle = g25->triangle;
                report.L2       = g25->L2;
                report.L1       = g25->L1;
                report.R2       = g25->R2;
                report.R1       = g25->R1;
                
                // Mantenemos tu configuración personalizada en la palanca de cambios
                report.select   = g25->L3;
                report.L3       = g25->select;
                report.R3       = g25->start;
                report.start    = g25->R3;
                
            } else if (wheel_pid == 0xc294) {
                // MODO DRIVING FORCE (Se usa solo durante el arranque hasta que se reinicia)
                df_report_t* df = (df_report_t*) report_;
                
                report.wheel    = df->wheel << 6;
                report.throttle = df->throttle << 8;
                report.brake    = df->brake << 8;
                
                report.dpad     = df->hat;
                report.cross    = df->cross;
                report.square   = df->square;
                report.circle   = df->circle;
                report.triangle = df->triangle;
                report.L2       = df->L2;
                report.L1       = df->L1;
                report.R2       = df->R2;
                report.R1       = df->R1;
                
                report.select   = df->L3;
                report.L3       = df->select;
                report.R3       = df->start;
                report.start    = df->R3;
            }
        }
    }

    tuh_hid_receive_report(dev_addr, instance);
}
