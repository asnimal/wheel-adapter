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

    report.PS = report.select && report.start;

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
        static uint8_t buf[] = { 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  // disable autocenter
        tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
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
                // printf(".");
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
        case 0xF1: {  // GET_SIGNATURE_NONCE
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
        case 0xF2: {  // GET_SIGNING_STATE
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
    if (report_id == 0xF0) {  // SET_AUTH_PAYLOAD
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
            // pass everything through to the wheel
            memcpy(ff_buf, buffer + 1, sizeof(ff_buf));
        }
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    printf("tuh_hid_mount_cb\n");
    
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    // Identificar si es un volante Logitech (Vendor ID 0x046D)
    if (vid == 0x046D) {
        wheel_device = dev_addr;
        wheel_instance = instance;
        
        // Si el volante no está en modo nativo G25 (cuyo PID es 0xC299)
        // Le enviamos el comando mágico para forzar el cambio.
        // Esto reiniciará el volante (se descentrará y volverá a calibrar correctamente).
        if (pid != 0xC299) {
            static uint8_t g25_native_cmd[7] = {0xf8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
            tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_FEATURE, g25_native_cmd, sizeof(g25_native_cmd));
            printf("Enviando comando de Modo Nativo al G25...\n");
        }
        
        tuh_hid_receive_report(dev_addr, instance);
        initialized = false;
    } else {  
        // Asumimos que cualquier otra cosa es el mando para la autenticación
        auth_device = dev_addr;
        auth_instance = instance;
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("tuh_hid_umount_cb\n");
    if (dev_addr == wheel_device) {
        wheel_device = 0;
        wheel_instance = 0;
    }
    if (dev_addr == auth_device) {
        auth_device = 0;
        auth_instance = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (len > 0) {
        if (dev_addr == wheel_device) {
            uint16_t vid, pid;
            tuh_vid_pid_get(dev_addr, &vid, &pid);

            // COMPROBAMOS SI ESTAMOS EN MODO NATIVO (PID 0xC299)
            if (pid == 0xC299 && len >= 12) {
                g25_native_report_t* native = (g25_native_report_t*) report_;

                // --- 1. VOLANTE Y PEDALES ---
                // Volante (de 14 bits a 16 bits que requiere el G29)
                uint16_t wheel_14 = native->x_lsb | (native->x_msb << 8);
                report.wheel = wheel_14 << 2;

                // Pedales (G25 nativo envía embrague real)
                report.throttle = native->throttle << 8;
                report.brake = native->brake << 8;
                report.clutch = native->clutch << 8;

                // --- 2. BOTONERA BÁSICA ---
                report.dpad = native->buttons1 & 0x0F;
                report.square = (native->buttons1 >> 4) & 1;
                report.cross = (native->buttons1 >> 5) & 1;
                report.circle = (native->buttons1 >> 6) & 1;
                report.triangle = (native->buttons1 >> 7) & 1;

                report.R1 = (native->buttons2 >> 0) & 1; // Leva derecha
                report.L1 = (native->buttons2 >> 1) & 1; // Leva izquierda
                report.R2 = (native->buttons2 >> 2) & 1; // Botón derecho del volante
                report.L2 = (native->buttons2 >> 3) & 1; // Botón izquierdo del volante
                
                // Botones negros de la palanca de cambios (los 4 en fila)
                report.select = (native->buttons2 >> 4) & 1; 
                report.start = (native->buttons2 >> 5) & 1;  
                report.R3 = (native->buttons2 >> 6) & 1;     
                report.L3 = (native->buttons2 >> 7) & 1;     

                // --- 3. BOTÓN PS (MUY IMPORTANTE) ---
                // Para que la PS5 detecte el volante, pulsa a la vez SELECT y START 
                // (los dos primeros botones negros de la palanca de cambios).
                report.PS = (report.select && report.start) ? 1 : 0;
                if (report.PS) { 
                    report.select = 0; // Limpiamos para no enviar otros comandos a la vez
                    report.start = 0;
                }

                // --- 4. MARCHAS EN H ---
                // El G29 mapea la palanca en el tercer byte del array "whatever"
                uint8_t g29_gears = 0;
                if (native->buttons3 & 0x02) g29_gears |= 0x01; // 1ª Marcha
                if (native->buttons3 & 0x04) g29_gears |= 0x02; // 2ª Marcha
                if (native->buttons3 & 0x08) g29_gears |= 0x04; // 3ª Marcha
                if (native->buttons3 & 0x10) g29_gears |= 0x08; // 4ª Marcha
                if (native->buttons3 & 0x20) g29_gears |= 0x10; // 5ª Marcha
                if (native->buttons3 & 0x40) g29_gears |= 0x20; // 6ª Marcha
                if (native->buttons3 & 0x01) g29_gears |= 0x40; // Marcha Atrás
                
                report.whatever[2] = g29_gears;

            } else {
                // FALLBACK: Si por lo que sea el volante aún está reiniciándose, 
                // usa el código original del Driving Force.
                df_report_t* df = (df_report_t*) report_;
                report.wheel = df->wheel << 6;
                report.throttle = df->throttle << 8;
                report.brake = df->brake << 8;
                report.dpad = df->hat;
                report.cross = df->cross;
                report.square = df->square;
                report.circle = df->circle;
                report.triangle = df->triangle;
                // ... deja aquí el resto de tu código original para el DF si lo prefieres ...
            }
        } else if (dev_addr == auth_device) {
            // (Tu código de autenticación de PS4 actual se queda intacto aquí)
    tuh_hid_receive_report(dev_addr, instance);
}
