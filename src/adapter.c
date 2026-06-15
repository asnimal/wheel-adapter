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
bool initialized = false;

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
    report.lx = 0x80; report.ly = 0x80; report.rx = 0x80; report.ry = 0x80;
    report.clutch = 0xFF00;
    memcpy(&prev_report, &report, sizeof(report));
}

void hid_task() {
    if (!tud_hid_ready()) return;

    // BOTÓN PS PERSONALIZADO: L3 + R3 encienden el mando en la PS5
    if (report.L3 && report.R3) {
        report.PS = 1;
    } else {
        report.PS = 0;
    }

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
        // Comando para forzar al G25 a activar su modo nativo completo
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

int main() {
    board_init(); report_init(); tusb_init(); stdio_init_all();
    while (1) {
        tuh_task(); tud_task(); hid_task(); auth_task(); wheel_init_task();
    }
    return 0;
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device) {
        busy = false;
        switch (report_id) {
            case 0xF3: state = SENDING_NONCE; break;
            case 0xF2: if (get_buffer[2] == 0) { signature_part = 0; state = RECEIVING_SIG; } break;
            case 0xF1:
                memcpy(signature + (signature_part * 56), get_buffer + 4, 56);
                signature_part++;
                if (signature_part == 19) { state = IDLE; expected_part = 0; signature_ready = true; signature_part = 0; }
                break;
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ((dev_addr == auth_device) && (report_id == 0xF0)) {
        busy = false;
        if (nonce_part == 5) state = WAITING_FOR_SIG;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    switch (report_id) {
        case 0x03: memcpy(buffer, output_0x03, reqlen); board_led_write(false); return reqlen;
        case 0xF3: memcpy(buffer, output_0xf3, reqlen); signature_ready = false; return reqlen;
        case 0xF1: {
            buffer[0] = nonce_id; buffer[1] = signature_part; buffer[2] = 0;
            memcpy(&buffer[3], &signature[signature_part * 56], 56);
            signature_part++;
            if (signature_part == 19) { signature_part = 0; board_led_write(true); }
            return reqlen;
        }
        case 0xF2: {
            buffer[0] = nonce_id; buffer[1] = signature_ready ? 0 : 16; memset(&buffer[2], 0, 9); return reqlen;
        }
    }
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (report_id == 0xF0) {
        uint8_t part = expected_part;
        if (bufsize == 63) { nonce_id = buffer[0]; part = buffer[1]; }
        if (part > 4) return;
        expected_part = part + 1;
        memcpy(&nonce[part * 56], &buffer[3], 56);
        if (part == 4) { nonce_ready = 1; state = SENDING_RESET; nonce_part = 0; }
    } else {
        if (bufsize > sizeof(ff_buf)) {
            memcpy(ff_buf, buffer + 1, sizeof(ff_buf));
        }
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint16_t vid; uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    if (vid == 0x046d && (pid == 0xc294 || pid == 0xc299)) { 
        // ¡SOLUCIÓN! Ahora acepta tanto el modo compatibilidad (c294) como el modo G25 Nativo (c299)
        wheel_device = dev_addr;
        wheel_instance = instance;
        tuh_hid_receive_report(dev_addr, instance);
        if (pid == 0xc299) initialized = true; // Si ya arrancó en modo nativo, no reenviar comando
    } else {
        auth_device = dev_addr;
        auth_instance = instance;
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr == wheel_device) { wheel_device = 0; wheel_instance = 0; initialized = false; }
    if (dev_addr == auth_device) { auth_device = 0; auth_instance = 0; }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (len > 0 && dev_addr == wheel_device) {
        g25_report_t* g25 = (g25_report_t*) report_;
        
        // Mapeo de Ejes e inversión de pedales para PS5
        report.wheel = g25->wheel << 2; 
        report.throttle = (255 - g25->throttle) << 8;
        report.brake = (255 - g25->brake) << 8;
        report.clutch = (255 - g25->clutch) << 8; 

        // Mapeo del D-Pad
        report.dpad = (g25->dpad < 8) ? g25->dpad : 8;

        // Botones del aro
        report.cross = (g25->buttons & 0x01) ? 1 : 0;
        report.square = (g25->buttons & 0x02) ? 1 : 0;
        report.R1 = (g25->buttons & 0x10) ? 1 : 0;
        report.L1 = (g25->buttons & 0x20) ? 1 : 0;

        // ORDEN PERSONALIZADO DE LOS BOTONES ROJOS (Select, L3, R3, Start)
        report.select = (g25->buttons & 0x40) ? 1 : 0; // 1º botón rojo
        report.L3 = (g25->buttons & 0x80) ? 1 : 0;     // 2º botón rojo
        report.R3 = (g25->buttons & 0x100) ? 1 : 0;    // 3º botón rojo
        report.start = (g25->buttons & 0x200) ? 1 : 0;  // 4º botón rojo

        // Botones auxiliares negros de la palanca
        report.circle = (g25->buttons & 0x04) ? 1 : 0;
        report.triangle = (g25->buttons & 0x08) ? 1 : 0;
        report.L2 = (g25->buttons & 0x400) ? 1 : 0;
        report.R2 = (g25->buttons & 0x0800) ? 1 : 0;

        // Palanca de cambios en H (Asignados a marchas virtuales)
        if (g25->buttons & 0x00010000) report.square |= 1; 
        if (g25->buttons & 0x00020000) report.cross |= 1;  
        if (g25->buttons & 0x00040000) report.circle |= 1; 
        if (g25->buttons & 0x00080000) report.triangle |= 1;
        if (g25->buttons & 0x00100000) report.R1 |= 1;     
        if (g25->buttons & 0x00200000) report.L1 |= 1;     
        if (g25->buttons & 0x00400000) report.R2 |= 1;     
    }

    tuh_hid_receive_report(dev_addr, instance);
}
