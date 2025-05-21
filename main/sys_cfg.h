#pragma once

#include <stdbool.h>
#include "esp_system.h"

// Define your settings structure
typedef struct {
    char ip_address[16];   // e.g., "192.168.1.100"
    char gateway[16];      // e.g., "192.168.1.1"
    char netmask[16];      // e.g., "255.255.255.0"
    bool dhcp_enable;
    bool dir_invert;
    int roller_limit;
    int roller_pos;
    int max_speed;
    char mqtt_uri[128];     // e.g., "mqtt://broker.hivemq.com"
    char mqtt_user[64];
    char mqtt_pass[64];
    char device_name[64]; // New setting
} device_settings_t;

extern device_settings_t settings;

extern esp_err_t save_settings(const device_settings_t *settings);

extern esp_err_t load_settings(device_settings_t *settings);

extern void print_settings(const device_settings_t *settings);
