#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"
#include "pico/stdlib.h"
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

int main() {
    board_init();
    report_init();
    tusb_init();
    
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    while (1) {
        tuh_task();
        tud_task();

        // Si ambos están conectados, mantenemos el LED encendido
        if (wheel_device != 0 && auth_device != 0) {
            gpio_put(PICO_DEFAULT_LED_PIN, true);
        } else {
            gpio_put(PICO_DEFAULT_LED_PIN, false);
        }

        if (tud_hid_ready()) {
            if (memcmp(&prev_report, &report, sizeof(report))) {
                tud_hid_report(1, &report, sizeof(report));
                memcpy(&prev_report, &report, sizeof(report));
            }
        }

        if (wheel_device && !initialized) {
            initialized = true;
            static uint8_t g25_native_mode[] = { 0xf8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00 };
            tuh_hid_send_report(wheel_device, wheel_instance, 0, g25_native_mode, sizeof(g25_native_mode));
        }

        if (!busy && auth_device) {
            switch (state) {
                case SENDING_RESET:
                    tuh_hid_get_report(auth_device, auth_instance, 0xF3, HID_REPORT_TYPE_FEATURE, get_buffer, 7 + 1);
                    busy = true; break;
                case SENDING_NONCE:
                    set_buffer[0] = 0xF0; set_buffer[1] = nonce_id; set_buffer[2] = nonce_part;
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
            if (signature_part == 19) signature_part = 0; return reqlen;
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
        wheel_device = dev_addr; wheel_instance = instance; tuh_hid_receive_report(dev_addr, instance);
    } else { auth_device = dev_addr; auth_instance = instance; }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr == wheel_device) { wheel_device = 0; initialized = false; }
    if (dev_addr == auth_device) auth_device = 0;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (len > 0 && dev_addr == wheel_device) {
        g25_report_t* g25 = (g25_report_t*) report_;
        
        // Reconstrucción matemática exacta del mapa de bits del G29
        uint32_t buttons_out = 0;

        // Aro del volante
        if (g25->buttons & 0x01)  buttons_out |= (1 << 1);  // Cross (X)
        if (g25->buttons & 0x02)  buttons_out |= (1 << 0);  // Square
        if (g25->buttons & 0x10)  buttons_out |= (1 << 5);  // R1
        if (g25->buttons & 0x20)  buttons_out |= (1 << 4);  // L1

        // BOTONES ROJOS DE LA PALANCA (Mapeo ordenado de Izquierda a Derecha)
        bool btn_select = (g25->buttons & 0x40)  ? true : false; // 1º botón rojo
        bool btn_l3     = (g25->buttons & 0x80)  ? true : false; // 2º botón rojo
        bool btn_r3     = (g25->buttons & 0x100) ? true : false; // 3º botón rojo
        bool btn_start  = (g25->buttons & 0x200) ? true : false; // 4º botón rojo

        if (btn_select) buttons_out |= (1 << 8);  // Select (Share)
        if (btn_l3)     buttons_out |= (1 << 10); // L3
        if (btn_r3)     buttons_out |= (1 << 11); // R3
        if (btn_start)  buttons_out |= (1 << 9);  // Start (Options)

        // ACCIÓN DEL BOTÓN PS: Si pulsas L3 + R3 (los dos centrales), se activa el bit de PS (bit 12)
        if (btn_l3 && btn_r3) {
            buttons_out |= (1 << 12); // Botón PS activo
        }

        // Botones auxiliares negros de la palanca
        if (g25->buttons & 0x04)   buttons_out |= (1 << 2);  // Circle
        if (g25->buttons & 0x08)   buttons_out |= (1 << 3);  // Triangle
        if (g25->buttons & 0x400)  buttons_out |= (1 << 6);  // L2
        if (g25->buttons & 0x0800) buttons_out |= (1 << 7);  // R2

        // Marchas físicas mapeadas a botones virtuales
        if (g25->buttons & 0x00010000) buttons_out |= (1 << 0); // 1ª marcha -> Square
        if (g25->buttons & 0x00020000) buttons_out |= (1 << 1); // 2ª marcha -> Cross
        if (g25->buttons & 0x00040000) buttons_out |= (1 << 2); // 3ª marcha -> Circle
        if (g25->buttons & 0x00080000) buttons_out |= (1 << 3); // 4ª marcha -> Triangle
        if (g25->buttons & 0x00100000) buttons_out |= (1 << 5); // 5ª marcha -> R1
        if (g25->buttons & 0x00200000) buttons_out |= (1 << 4); // 6ª marcha -> L1
        if (g25->buttons & 0x00400000) buttons_out |= (1 << 7); // Marcha atrás -> R2

        // Escribir los bits de forma nativa en la estructura de salida hacia PS5
        uint8_t* report_raw = (uint8_t*)&report;
        
        // Copiar D-Pad y los 3 bytes de botones en las posiciones exactas del G29
        report_raw[4] = (g25->dpad < 8) ? g25->dpad : 8;
        report_raw[4] |= ((buttons_out & 0x0F) << 4); 
        report_raw[5] = (buttons_out >> 4) & 0xFF;
        report_raw[6] = (buttons_out >> 12) & 0xFF;

        // Ejes analógicos mapeados
        report.wheel = g25->wheel << 2;
        report.throttle = (255 - g25->throttle) << 8;
        report.brake = (255 - g25->brake) << 8;
        report.clutch = (255 - g25->clutch) << 8; // Soporte completo de embrague
    }
    tuh_hid_receive_report(dev_addr, instance);
}
