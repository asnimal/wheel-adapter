#include <stdio.h>
#include <string.h>
#include "bsp/board_api.h"
#include "tusb.h"
#include "reports.h"

uint8_t wheel_device = 0, wheel_instance = 0;
g29_report_t report;
g29_report_t prev_report;

void report_init() {
    memset(&report, 0, sizeof(report));
    report.lx = 0x80; report.ly = 0x80; report.rx = 0x80; report.ry = 0x80;
    memcpy(&prev_report, &report, sizeof(report));
}

void hid_task() {
    if (!tud_hid_ready()) return;

    // SIMULACIÓN BOTÓN PS: Si pulsas Select y Start a la vez, se activa el PS
    report.PS = (report.select && report.start);

    if (memcmp(&prev_report, &report, sizeof(report))) {
        tud_hid_report(1, &report, sizeof(report));
        memcpy(&prev_report, &report, sizeof(report));
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report_, uint16_t len) {
    if (dev_addr == wheel_device && len >= 12) {
        g25_native_report_t* native = (g25_native_report_t*) report_;

        // CALIBRACIÓN: Combinamos LSB y MSB para obtener la posición real del volante
        uint16_t wheel_val = (native->x_lsb | (native->x_msb << 8));
        report.wheel = wheel_val;

        report.throttle = native->throttle << 8;
        report.brake = native->brake << 8;
        report.clutch = native->clutch << 8;

        // MAPEO DE BOTONES
        report.select = (native->buttons2 & 0x10) ? 1 : 0;
        report.start = (native->buttons2 & 0x20) ? 1 : 0;
        report.cross = (native->buttons1 & 0x20) ? 1 : 0;
        report.square = (native->buttons1 & 0x10) ? 1 : 0;
        report.circle = (native->buttons1 & 0x40) ? 1 : 0;
        report.triangle = (native->buttons1 & 0x80) ? 1 : 0;
    }
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc, uint16_t len) {
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    if (vid == 0x046d) { // Logitech
        wheel_device = dev_addr;
        wheel_instance = instance;
        tuh_hid_receive_report(dev_addr, instance);
    }
}

int main() {
    board_init();
    report_init();
    tusb_init();
    while (1) {
        tuh_task();
        tud_task();
        hid_task();
    }
}
