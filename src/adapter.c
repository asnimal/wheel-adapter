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
uint32_t mount_time = 0;

uint8_t get_buffer[64];
uint8_t set_buffer[64];
uint8_t ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t prev_ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

g29_report_t report;
g29_report_t prev_report;

// G29 descriptor output standard
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
    report.wheel = 0x8000; // Centrado absoluto por defecto (evita desvíos al arrancar)
    report.throttle = 0;
    report.brake = 0;
    report.clutch = 0;
    memcpy(&prev_report, &report, sizeof(report));
}

void hid_task() {
    if (!tud_hid_ready()) {
        return;
    }

    // Botón PS combinación select + start seguro
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
        // Damos 4 segundos completos para asegurar que el autocalibrado motorizado termine sin cortes
        if (board_millis() - mount_time > 4000) {
            initialized = true;
            if (wheel_pid == 0xc294) {
                // Comando extendido e inequívoco para activar el G25 en modo completo de 5 ejes y embrague
                static uint8_t buf[] = { 0xf8, 0x12, 0x02, 0x00, 0x00, 0x00, 0x00 };
                tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
            } else {
                // Desactivar centrado artificial una vez estabilizado en modo nativo
                static uint8_t buf[] = { 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
                tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
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

    if ((vid == 0x046d) && ((pid == 0xc294) || (pid == 0xc299))) {  
        wheel_device = dev_addr;
        wheel_instance = instance;
        wheel_pid = pid;
        mount_time = board_millis();
        initialized = false;
        tuh_hid_receive_report(dev_addr, instance);
    } else {  
        auth_device = dev_addr;
        auth_instance = instance;
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
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
    if (len > 0 && dev_addr == wheel_device) {
        if (wheel_pid == 0xc299) {
            // RE-MAPEO INTEGRAL PARA EL MODO NATIVO DEL G25 CON REGLA DE 3 PEDALES Y PALANCA
            
            // 1. Dirección (Alineamos los 14 bits nativos del volante al rango esperado)
            uint16_t raw_wheel = report_[0] | ((report_[1] & 0x3F) << 8);
            report.wheel = raw_wheel << 2; 

            // 2. Pedales analógicos reales (Del 0 al 255 invertidos, desplazados a los bytes exactos)
            report.throttle = (255 - report_[2]) << 8;
            report.brake    = (255 - report_[3]) << 8;
            report.clutch   = (255 - report_[4]) << 8;

            // 3. Desenredar Botones y Cruceta (Limpieza estricta de bits fantasmas)
            report.dpad     = report_[5] & 0x0F;
            report.square   = (report_[5] & 0x10) ? 1 : 0;
            report.cross    = (report_[5] & 0x20) ? 1 : 0;
            report.circle   = (report_[5] & 0x40) ? 1 : 0;
            report.triangle = (report_[5] & 0x80) ? 1 : 0;

            report.L1       = (report_[6] & 0x01) ? 1 : 0;
            report.R1       = (report_[6] & 0x02) ? 1 : 0;
            report.L2       = (report_[6] & 0x04) ? 1 : 0;
            report.R2       = (report_[6] & 0x08) ? 1 : 0;
            report.select   = (report_[6] & 0x10) ? 1 : 0;
            report.start    = (report_[6] & 0x20) ? 1 : 0;
            report.L3       = (report_[6] & 0x40) ? 1 : 0;
            report.R3       = (report_[6] & 0x80) ? 1 : 0;

        } else if (wheel_pid == 0xc294) {
            // Mapeo seguro provisional durante el arranque en frío
            df_report_t* df = (df_report_t*) report_;
            report.wheel    = df->wheel << 6;
            report.throttle = df->throttle << 8;
            report.brake    = df->brake << 8;
            report.clutch   = 0;
            report.dpad     = df->hat;
            report.cross    = df->cross;
            report.square   = df->square;
            report.circle   = df->circle;
            report.triangle = df->triangle;
            report.L2       = df->L2;
            report.L1       = df->L1;
            report.R2       = df->R2;
            report.R1       = df->R1;
            report.select   = df->select;
            report.start    = df->start;
            report.R3       = df->R3;
            report.L3       = df->L3;
        }
    }
    tuh_hid_receive_report(dev_addr, instance);
}
