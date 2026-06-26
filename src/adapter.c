#include <stdlib.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "reports.h"

// Alineación estricta de hardware a bloques de 4 bytes para todos los búferes de transmisión USB
CFG_TUSB_MEM_ALIGN uint8_t nonce[280];
CFG_TUSB_MEM_ALIGN uint8_t signature[1064];
CFG_TUSB_MEM_ALIGN uint8_t get_buffer[64];
CFG_TUSB_MEM_ALIGN uint8_t set_buffer[64];

uint8_t nonce_id;
uint8_t nonce_part = 0;
uint8_t signature_part = 0;
uint8_t signature_ready = 0;
uint8_t expected_part = 0;

uint8_t wheel_device = 0;
uint8_t wheel_instance = 0; 
uint16_t wheel_pid = 0;       
uint8_t auth_device = 0;
uint8_t auth_instance = 0;

bool busy = false;
uint32_t auth_timer = 0;             // Temporizador para evitar el bloqueo de autenticación
uint32_t last_wheel_report_time = 0; // Temporizador para el Watchdog del volante G25

enum {
    IDLE = 0,
    SENDING_RESET = 1,
    SENDING_NONCE = 2,
    WAITING_FOR_SIG = 3,
    RECEIVING_SIG = 4,
};

uint8_t state = IDLE;

bool initialized = true;
bool calibration_done = false;

CFG_TUSB_MEM_ALIGN uint8_t ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
CFG_TUSB_MEM_ALIGN uint8_t prev_ff_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

CFG_TUSB_MEM_ALIGN g29_report_t report;
CFG_TUSB_MEM_ALIGN g29_report_t prev_report;

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
    report.throttle = 0xFFFF;
    report.brake = 0xFFFF;
    report.clutch = 0xFFFF;
    memcpy(&prev_report, &report, sizeof(report));
}

void hid_task() {
    if (!tud_hid_ready()) {
        return;
    }

    // Watchdog de auto-recuperación activa de datos del volante ante ruidos electromagnéticos
    uint32_t current_time = board_millis();
    if (wheel_device && (current_time - last_wheel_report_time >= 100)) {
        last_wheel_report_time = current_time;
        tuh_hid_receive_report(wheel_device, wheel_instance);
    }

    // Combinación física real para el botón PS (L3 + R3)
    report.PS = report.L3 && report.R3;
    if (memcmp(&prev_report, &report, sizeof(report))) {
        tud_hid_report(1, &report, sizeof(report));
        memcpy(&prev_report, &report, sizeof(report));
    }

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
            static uint8_t cmd_g25_native[] = { 0xf8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00 };
            tuh_hid_set_report(wheel_device, wheel_instance, 0, HID_REPORT_TYPE_OUTPUT, cmd_g25_native, sizeof(cmd_g25_native));
            wheel_pid = 0;
        } 
        else if (wheel_pid == 0xc299) {
            if (!initialized) {
                initialized = true;
                calibration_done = false;
                c299_mount_time = current_time;
            }

            if (!calibration_done && (current_time - c299_mount_time >= 6000)) {
                calibration_done = true;
                static uint8_t buf[] = { 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
                tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
                tuh_hid_set_report(wheel_device, wheel_instance, 0, HID_REPORT_TYPE_OUTPUT, buf, sizeof(buf));
            }
        }
    }
}

void auth_task() {
    if (!auth_device) return;

    uint32_t current_time = board_millis();

    // Sistema de auto-recuperación ante pérdida de paquetes (Timeout de 2000ms)
    if (busy && (current_time - auth_timer >= 2000)) {
        busy = false;
        if (state == SENDING_NONCE && nonce_part > 0) {
            nonce_part--; 
        }
        if (state == RECEIVING_SIG && signature_part > 0) {
            signature_part--; 
        }
    }

    if (!busy) {
        switch (state) {
            case IDLE:
                break;
            case SENDING_RESET:
                tuh_hid_get_report(auth_device, auth_instance, 0xF3, HID_REPORT_TYPE_FEATURE, get_buffer, 7 + 1);
                busy = true;
                auth_timer = current_time;
                break;
            case SENDING_NONCE:
                set_buffer[0] = 0xF0;
                set_buffer[1] = nonce_id;
                set_buffer[2] = nonce_part;
                set_buffer[3] = 0;
                memcpy(set_buffer + 4, nonce + (nonce_part * 56), 56);
                tuh_hid_set_report(auth_device, auth_instance, 0xF0, HID_REPORT_TYPE_FEATURE, set_buffer, 64);
                busy = true;
                auth_timer = current_time;
                nonce_part++;
                break;
            case WAITING_FOR_SIG:
                tuh_hid_get_report(auth_device, auth_instance, 0xF2, HID_REPORT_TYPE_FEATURE, get_buffer, 15 + 1);
                busy = true;
                auth_timer = current_time;
                break;
            case RECEIVING_SIG:
                tuh_hid_get_report(auth_device, auth_instance, 0xF1, HID_REPORT_TYPE_FEATURE, get_buffer, 63 + 1);
                busy = true;
                auth_timer = current_time;
                break;
        }
    }
}

int main() {
    board_init();
    report_init();
    tusb_init(); // Inicializa el bus USB a máxima prioridad de hardware, sin canales de texto lentos

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
                } else {
                    state = WAITING_FOR_SIG;
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
                } else {
                    state = RECEIVING_SIG;
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
        } else {
            state = SENDING_NONCE;
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
        last_wheel_report_time = board_millis(); // Inicializa el temporizador del Watchdog
        tuh_hid_receive_report(dev_addr, instance);
        initialized = false;
        calibration_done = false;
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
        calibration_done = false;
    }
    if (dev_addr == auth_device) {
        auth_device = 0;
        auth_instance = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (len > 0 && dev_addr == wheel_device) {
        // Marcamos la hora del último reporte de datos analógicos recibido con éxito del volante
        last_wheel_report_time = board_millis();

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
            // TRADUCCIÓN ESTRICTA G25 (C299) -> PROTOCOLO OFICIAL G29
            // =================================================================

            // 1. DIRECCIÓN
            uint16_t raw_steering = report_[3] | (report_[4] << 8);
            report.wheel = raw_steering;

            // 2. PEDALES DE CARRERA CON FILTRADO COMPLETO DE RUIDO ANALÓGICO EN REPOSO
            // Acelerador
            if (report_[5] >= 0xFA) {
                report.throttle = 0xFFFF;
            } else {
                report.throttle = report_[5] << 8;
            }

            // Freno
            if (report_[6] >= 0xFA) {
                report.brake = 0xFFFF;
            } else {
                report.brake = report_[6] << 8;
            }

            // Embrague
            if (report_[7] >= 0xFA) {
                report.clutch = 0xFFFF; 
            } else {
                report.clutch = report_[7] << 8; 
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
            report.R1       = (report_[1] & 0x01) ? 1 : 0; 
            report.L1       = (report_[1] & 0x02) ? 1 : 0; 
            report.R2       = (report_[1] & 0x04) ? 1 : 0; 
            report.L2       = (report_[1] & 0x08) ? 1 : 0; 

            // 6. TRADUCCIÓN DE BOTONES ROJOS Y PALANCA EN MODO SECUENCIAL / H-PATTERN
            report.select   = (report_[1] & 0x80) ? 1 : 0; 
            report.start    = (report_[1] & 0x40) ? 1 : 0; 

            // L3 (Botón rojo 2 OR palanca hacia adelante en secuencial/1ª marcha)
            bool push_forward = (report_[2] == 0x81) || (report_[2] == 0x01);
            report.L3       = ((report_[1] & 0x10) || push_forward) ? 1 : 0;

            // R3 (Botón rojo 3 OR palanca hacia atrás en secuencial/2ª marcha)
            bool pull_backward = (report_[2] == 0x82) || (report_[2] == 0x02);
            report.R3       = ((report_[1] & 0x20) || pull_backward) ? 1 : 0;
        }
    }

    tuh_hid_receive_report(dev_addr, instance);
}
