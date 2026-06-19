#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/board_api.h"
#include "tusb.h"
#include "pico/stdio.h"
#include "reports.h"

// Variables globales
uint8_t nonce_id, nonce[280], nonce_part = 0, signature[1064], signature_part = 0;
uint8_t signature_ready = 0, nonce_ready = 0, expected_part = 0;
uint8_t wheel_device = 0, wheel_instance = 0, auth_device = 0, auth_instance = 0;
bool busy = false;

enum { IDLE = 0, SENDING_RESET = 1, SENDING_NONCE = 2, WAITING_FOR_SIG = 3, RECEIVING_SIG = 4 };
uint8_t state = IDLE;
bool initialized = true;

uint8_t get_buffer[64], set_buffer[64], ff_buf[] = { 0,0,0,0,0,0,0 }, prev_ff_buf[] = { 0,0,0,0,0,0,0 };
g29_report_t report, prev_report;

const uint8_t output_0x03[] = { 0x21, 0x27, 0x03, 0x11, 0x06, 0,0,0,0,0,0,0,0,0,0, 0,0, 0x0D, 0x0D, 0,0,0,0, 0x0D, 0x84, 0x03, 0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
const uint8_t output_0xf3[] = { 0, 0x38, 0x38, 0, 0, 0, 0 };

void report_init() {
    memset(&report, 0, sizeof(report));
    report.lx = 0x80; report.ly = 0x80; report.rx = 0x80; report.ry = 0x80;
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
        if (wheel_device) tuh_hid_send_report(wheel_device, wheel_instance, 0, ff_buf, sizeof(ff_buf));
        memcpy(prev_ff_buf, ff_buf, sizeof(ff_buf));
    }
}

void wheel_init_task() {
    if (wheel_device && !initialized) {
        initialized = true;
        static uint8_t buf[] = { 0xf5, 0,0,0,0,0,0 };
        tuh_hid_send_report(wheel_device, wheel_instance, 0, buf, sizeof(buf));
    }
}

void auth_task() {
    if (!busy && auth_device) {
        switch (state) {
            case SENDING_RESET: tuh_hid_get_report(auth_device, auth_instance, 0xF3, HID_REPORT_TYPE_FEATURE, get_buffer, 8); busy = true; break;
            case SENDING_NONCE: 
                set_buffer[0] = 0xF0; set_buffer[1] = nonce_id; set_buffer[2] = nonce_part; set_buffer[3] = 0;
                memcpy(set_buffer + 4, nonce + (nonce_part * 56), 56);
                tuh_hid_set_report(auth_device, auth_instance, 0xF0, HID_REPORT_TYPE_FEATURE, set_buffer, 64);
                busy = true; nonce_part++; break;
            case WAITING_FOR_SIG: tuh_hid_get_report(auth_device, auth_instance, 0xF2, HID_REPORT_TYPE_FEATURE, get_buffer, 16); busy = true; break;
            case RECEIVING_SIG: tuh_hid_get_report(auth_device, auth_instance, 0xF1, HID_REPORT_TYPE_FEATURE, get_buffer, 64); busy = true; break;
        }
    }
}

int main() {
    board_init(); report_init(); tusb_init(); stdio_init_all();
    while (1) { tuh_task(); tud_task(); hid_task(); auth_task(); wheel_init_task(); }
    return 0;
}

// Callbacks USB
void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device) {
        busy = false;
        if (report_id == 0xF3) state = SENDING_NONCE;
        else if (report_id == 0xF2 && get_buffer[2] == 0) { signature_part = 0; state = RECEIVING_SIG; }
        else if (report_id == 0xF1) {
            memcpy(signature + (signature_part * 56), get_buffer + 4, 56);
            if (++signature_part == 19) { state = IDLE; signature_ready = true; signature_part = 0; }
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if (dev_addr == auth_device && report_id == 0xF0) { busy = false; if (nonce_part == 5) state = WAITING_FOR_SIG; }
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    if (report_id == 0x03) { memcpy(buffer, output_0x03, reqlen); return reqlen; }
    if (report_id == 0xF3) { memcpy(buffer, output_0xf3, reqlen); signature_ready = false; return reqlen; }
    if (report_id == 0xF1) {
        buffer[0] = nonce_id; buffer[1] = signature_part; buffer[2] = 0;
        memcpy(&buffer[3], &signature[signature_part * 56], 56);
        if (++signature_part == 19) signature_part = 0;
        return reqlen;
    }
    if (report_id == 0xF2) { buffer[0] = nonce_id; buffer[1] = signature_ready ? 0 : 16; memset(&buffer[2], 0, 9); return reqlen; }
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (report_id == 0xF0) {
        uint8_t part = (bufsize == 63) ? buffer[1] : expected_part;
        if (part <= 4) {
            expected_part = part + 1;
            memcpy(&nonce[part * 56], &buffer[3], 56);
            if (part == 4) { nonce_ready = 1; state = SENDING_RESET; nonce_part = 0; }
        }
    } else if (bufsize > 1) memcpy(ff_buf, buffer + 1, sizeof(ff_buf));
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint16_t vid, pid; tuh_vid_pid_get(dev_addr, &vid, &pid);
    if (vid == 0x046d && pid == 0xc294) { wheel_device = dev_addr; wheel_instance = instance; tuh_hid_receive_report(dev_addr, instance); initialized = false; }
    else { auth_device = dev_addr; auth_instance = instance; }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr == wheel_device) wheel_device = 0;
    if (dev_addr == auth_device) auth_device = 0;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (len > 0 && dev_addr == wheel_device) {
        df_report_t* df = (df_report_t*) report_;
        report.wheel = df->wheel << 6; report.throttle = df->throttle << 8; report.brake = df->brake << 8; report.dpad = df->hat;
        report.cross = df->cross; report.square = df->square; report.circle = df->circle; report.triangle = df->triangle;
        report.L2 = df->L2; report.L1 = df->L1; report.R2 = df->R2; report.R1 = df->R1;
        report.select = df->select; report.start = df->start; report.R3 = df->R3; report.L3 = df->L3;
    }
    tuh_hid_receive_report(dev_addr, instance);
}
