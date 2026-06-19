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
    if (!tud_hid_ready()) return;
    
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
        // Comando para desactivar el autocenter
        static uint8_t buf[] = { 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
    }
}

void auth_task() {
    if (!busy && auth_device) {
        switch (state) {
            case IDLE: break;
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
            case 0xF3: state = SENDING_NONCE; break;
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
        if (part > 4) return;
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
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    
    if ((vid == 0x046d) && (pid == 0xc294 || pid == 0xc299)) {
        wheel_device = dev_addr;
        wheel_instance = instance;
        tuh_hid_receive_report(dev_addr, instance);
        initialized = (pid == 0xc299);
    } else {
        auth_device = dev_addr;
        auth_instance = instance;
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
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
    if (len > 0 && dev_addr == wheel_device) {
        uint16_t vid, pid;
        tuh_vid_pid_get(dev_addr, &vid, &pid);
        
        if (pid == 0xc294) {
            // MODO COMPATIBILIDAD (0xC294) - RESTAURADO AL ORIGINAL PARA QUE EL VOLANTE SE CENTRE
            df_report_t* df = (df_report_t*) report_;
            report.wheel = df->wheel << 6;
            report.throttle = df->throttle << 8;
            report.brake = df->brake << 8;
            report.dpad = df->hat;
            report.cross = df->cross;
            report.square = df->square;
            report.circle = df->circle;
            report.triangle = df->triangle;
            report.L2 = df->L2;
            report.L1 = df->L1;
            report.R2 = df->R2;
            report.R1 = df->R1;
            report.select = df->select;
            report.start = df->start;
            report.R3 = df->R3;
            report.L3 = df->L3;
            
            // ¡NUEVO! EL CLUTCH EN MODO COMPATIBILIDAD VIAJA EN EL BYTE 8 (ÍNDICE 7)
            if (len >= 8) {
                uint8_t clutch_raw = report_[7];
                uint8_t clutch = 255 - clutch_raw; // Invertir para que 0 sea sin pisar
                report.clutch = clutch << 8;       // Alta resolución para GT7
                report.whatever[2] = clutch;       // Bloque oculto para GT7
            }
        }
        else if (pid == 0xc299 && len >= 8) {
            // MODO NATIVO (0xC299) - AQUÍ SÍ EXISTE LA PALANCA EN H
            uint8_t const* d = report_;
            
            // Volante
            uint16_t raw_wheel = d[0] | ((d[1] & 0x03) << 8);
            report.wheel = raw_wheel << 6;
            
            // Pedales
            uint8_t gas = 255 - d[5];
            uint8_t brake = 255 - d[6];
            uint8_t clutch = 255 - d[7];
            
            report.throttle = gas << 8;
            report.brake = brake << 8;
            report.clutch = clutch << 8;
            
            report.whatever[0] = gas;
            report.whatever[1] = brake;
            report.whatever[2] = clutch;
            
            // Botones y D-Pad
            report.cross = (d[1] & 0x04) ? 1 : 0;
            report.square = (d[1] & 0x08) ? 1 : 0;
            report.circle = (d[1] & 0x10) ? 1 : 0;
            report.triangle = (d[1] & 0x20) ? 1 : 0;
            report.R1 = (d[1] & 0x40) ? 1 : 0;
            report.L1 = (d[1] & 0x80) ? 1 : 0;
            
            report.R2 = (d[2] & 0x01) ? 1 : 0;
            report.L2 = (d[2] & 0x02) ? 1 : 0;
            report.select = (d[2] & 0x04) ? 1 : 0;
            report.start = (d[2] & 0x08) ? 1 : 0;
            report.R3 = (d[2] & 0x10) ? 1 : 0;
            report.L3 = (d[2] & 0x20) ? 1 : 0;
            
            uint8_t hat = d[3] & 0x0F;
            report.dpad = (hat < 8) ? hat : 8;
            
            // Levas secuenciales (mapeadas a R2/L2)
            if (d[3] & 0x40) report.R2 = 1; 
            if (d[3] & 0x80) report.L2 = 1; 
            
            // Palanca en H (Byte 4)
            report.whatever[3] = 0;
            if (d[4] & 0x01) report.whatever[3] |= (1 << 0);
            if (d[4] & 0x02) report.whatever[3] |= (1 << 1);
            if (d[4] & 0x04) report.whatever[3] |= (1 << 2);
            if (d[4] & 0x08) report.whatever[3] |= (1 << 3);
            if (d[4] & 0x10) report.whatever[3] |= (1 << 4);
            if (d[4] & 0x20) report.whatever[3] |= (1 << 5);
            if (d[4] & 0x40) report.whatever[3] |= (1 << 6);
            
            report.touchpad = (d[4] & 0x80) ? 1 : 0;
        }
    }
    tuh_hid_receive_report(dev_addr, instance);
}
