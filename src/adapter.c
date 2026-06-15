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
enum { IDLE = 0, SENDING_RESET = 1, SENDING_NONCE = 2, WAITING_FOR_SIG = 3, RECEIVING_SIG = 4 };
uint8_t state = IDLE;
bool initialized = false;

uint8_t get_buffer[64];
uint8_t set_buffer[64];
uint8_t ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t prev_ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

g29_report_t report;
g29_report_t prev_report;

// Control de tiempo y estado del LED con tipo bool estricto para el SDK de Pico
uint32_t last_led_blink_time = 0;
bool led_state = false; 

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
    report.lx = 0x80; report.ly = 0x80; report.rx = 0x80; report.ry = 0x80;
    report.clutch = 0xFFFF; 
    memcpy(&prev_report, &report, sizeof(report));
}

void hid_task() {
    if (!tud_hid_ready()) return;

    // Acción del botón PS combinando físicamente L3 y R3
    report.PS = (report.L3 && report.R3) ? 1 : 0;

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
        static uint8_t g25_native_mode[] = { 0xf8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00 };
        tuh_hid_send_report(wheel_device, wheel_instance, 0, g25_native_mode, sizeof(g25_native_mode));
    }
}

void auth_task() {
    if (!busy && auth_device) {
        switch (state) {
            case IDLE: break;
            case SENDING_RESET:
                tuh_hid_get_report(auth_device, auth_instance, 0xF3, HID_REPORT_TYPE_FEATURE, get_buffer, 7 + 1);
                busy = true; break;
            case SENDING_NONCE:
                set_buffer[0] = 0xF0; set_buffer[1] = nonce_id; set_buffer[2] = nonce_part; set_buffer[3] = 0;
                memcpy(set_buffer + 4, nonce + (nonce_part * 56), 56);
                tuh_hid_set_report(auth_device, auth_instance, 0xF0, HID_REPORT_TYPE_FEATURE, set_buffer, 64);
                busy = true; nonce_part++; break;
            case WAITING_FOR_SIG:
                tuh_hid_get_report(auth_device, auth_instance, 0xF2, HID_REPORT_TYPE_FEATURE, get_buffer, 15 + 1);
                busy = true; break;
            case RECEIVING_SIG:
                tuh_hid_get_report(auth_device, auth_instance, 0xF1, HID_REPORT_TYPE_FEATURE, get_buffer, 63 + 1);
                busy = true; break;
        }
    }
}

void led_status_task() {
    uint32_t current_time = board_ticks_to_ms(board_ticks());
    
    if (wheel_device == 0 || auth_device == 0) {
        // Estado 1: Falta hardware -> Parpadeo lento (800ms)
        if (current_time - last_led_blink_time >= 800) {
            last_led_blink_time = current_time;
            led_state = !led_state;
            board_led_write(led_state);
        }
    } else if (signature_ready == 0) {
        // Estado 2: Esperando firma de PS5 -> Parpadeo rápido (100ms)
        if (current_time - last_led_blink_time >= 100) {
            last_led_blink_time = current_time;
            led_state = !led_state;
            board_led_write(led_state);
        }
    } else {
        // Estado 3: Todo correcto -> Encendido fijo
        board_led_write(true);
    }
}

int main() {
    board_init(); report_init(); tusb_init(); stdio_init_all();
    while (1) {
        tuh_task(); tud_task(); hid_task(); auth_task(); wheel_init_task();
        led_status_task(); 
    }
    return 0;
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device) { busy = false;
        switch (report_id) {
            case 0xF3: state = SENDING_NONCE; break;
            case 0xF2: if (get_buffer[2] == 0) { signature_part = 0; state = RECEIVING_SIG; } break;
            case 0xF1: memcpy(signature + (signature_part * 56), get_buffer + 4, 56); signature_part++;
                if (signature_part == 19) { state = IDLE; expected_part = 0; signature_ready = true; signature_part = 0; } break;
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ((dev_addr == auth_device) && (report_id == 0xF0)) { busy = false; if (nonce_part == 5) state = WAITING_FOR_SIG; }
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    switch (report_id) {
        case 0x03: memcpy(buffer, output_0x03, reqlen); return reqlen;
        case 0xF3: memcpy(buffer, output_0xf3, reqlen); signature_ready = false; return reqlen;
        case 0xF1: buffer[0] = nonce_id; buffer[1] = signature_part; buffer[2] = 0;
            memcpy(&buffer[3], &signature[signature_part * 56], 56); signature_part++;
            if (signature_part == 19) { signature_part = 0; signature_ready = true; } return reqlen;
        case 0xF2: buffer[0] = nonce_id; buffer[1] = signature_ready ? 0 : 16; memset(&buffer[2], 0, 9); return reqlen;
    }
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (report_id == 0xF0) {
        uint8_t part = expected_part;
        if (bufsize == 63) { nonce_id = buffer[0]; part = buffer[1]; }
        if (part > 4) return;
        expected_part = part + 1; memcpy(&nonce[part * 56], &buffer[3], 56);
        if (part == 4) { nonce_ready = 1; state = SENDING_RESET; nonce_part = 0; }
    } else {
        if (bufsize > sizeof(ff_buf)) { memcpy(ff_buf, buffer + 1, sizeof(ff_buf)); }
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint16_t vid, pid; tuh_vid_pid_get(dev_addr, &vid, &pid);

    if (vid == 0x046d && (pid == 0xc294 || pid == 0xc299)) { 
        wheel_device = dev_addr; wheel_instance = instance; 
        if (pid == 0xc299) initialized = true; 
        tuh_hid_receive_report(dev_addr, instance);
    } else { 
        auth_device = dev_addr; auth_instance = instance; 
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr == wheel_device) { wheel_device = 0; initialized = false; }
    if (dev_addr == auth_device) auth_device = 0;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (len > 0 && dev_addr == wheel_device) {
        uint16_t vid, pid;
        tuh_vid_pid_get(dev_addr, &vid, &pid);
        
        if (pid == 0xc294) {
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
            report.R1 = df->R1;
            report.L1 = df->L1;
            report.R2 = df->R2;
            report.L2 = df->L2;
            report.select = df->select;
            report.L3 = df->L3;
            report.R3 = df->R3;
            report.start = df->start;
            
        } else if (pid == 0xc299) {
            // Mapeo seguro directo por lectura de buffer indexado
            if (len >= 8) {
                uint16_t raw_wheel = report_[0] | ((report_[1] & 0x3F) << 8);
                report.wheel = raw_wheel << 2;
                
                report.cross    = (report_[1] & 0x40) ? 1 : 0;
                report.square   = (report_[1] & 0x80) ? 1 : 0;
                report.circle   = (report_[2] & 0x01) ? 1 : 0;
                report.triangle = (report_[2] & 0x02) ? 1 : 0;
                report.R1       = (report_[2] & 0x04) ? 1 : 0;
                report.L1       = (report_[2] & 0x08) ? 1 : 0;
                report.R2       = (report_[2] & 0x10) ? 1 : 0;
                report.L2       = (report_[2] & 0x20) ? 1 : 0;
                
                // Distribución física exacta solicitada de los botones rojos
                report.select   = (report_[2] & 0x40) ? 1 : 0; 
                report.L3       = (report_[2] & 0x80) ? 1 : 0; 
                report.R3       = (report_[3] & 0x01) ? 1 : 0; 
                report.start    = (report_[3] & 0x02) ? 1 : 0; 
                
                // Mapeo de velocidades físicas de la palanca
                if (report_[3] & 0x04) report.square |= 1;  
                if (report_[3] & 0x08) report.cross  |= 1;  
                if (report_[3] & 0x10) report.circle |= 1;  
                if (report_[3] & 0x20) report.triangle |= 1; 
                if (report_[3] & 0x40) report.R1     |= 1;  
                if (report_[3] & 0x80) report.L1     |= 1;  
                
                uint8_t hat = report_[4] & 0x0F;
                report.dpad = (hat < 8) ? hat : 8;
                if (report_[4] & 0x10) report.R2 |= 1; 
                
                // Mapeo analógico de pedales
                report.throttle = (255 - report_[5]) << 8;
                report.brake    = (255 - report_[6]) << 8;
                report.clutch   = (255 - report_[7]) << 8; 
            }
        }
    }
    tuh_hid_receive_report(dev_addr, instance);
}
