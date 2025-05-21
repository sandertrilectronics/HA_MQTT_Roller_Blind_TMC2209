#include "sys_cfg.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>

#define SETTINGS_FILE_PATH "/spiffs/settings.json"

static const char *TAG = "settings";

// Default settings
device_settings_t settings = {
    .ip_address = "192.168.1.100",
    .gateway = "192.168.1.1",
    .netmask = "255.255.255.0",
    .dhcp_enable = true,
    .dir_invert = false,
    .roller_limit = 1600,
    .roller_pos = 0,
    .max_speed = 150,
    .mqtt_uri = "mqtt://broker.hivemq.com",
    .mqtt_user = "user",
    .mqtt_pass = "pass",
    .device_name = "Roller Blind"
};

// Save settings to JSON file (takes pointer to settings)
esp_err_t save_settings(const device_settings_t *settings) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create cJSON object");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "ip_address", settings->ip_address);
    cJSON_AddStringToObject(root, "gateway", settings->gateway);
    cJSON_AddStringToObject(root, "netmask", settings->netmask);
    cJSON_AddBoolToObject(root, "dhcp_enable", settings->dhcp_enable);
    cJSON_AddBoolToObject(root, "dir_invert", settings->dir_invert);
    cJSON_AddNumberToObject(root, "roller_limit", settings->roller_limit);
    cJSON_AddNumberToObject(root, "roller_pos", settings->roller_pos);
    cJSON_AddNumberToObject(root, "max_speed", settings->max_speed);
    cJSON_AddStringToObject(root, "mqtt_uri", settings->mqtt_uri);
    cJSON_AddStringToObject(root, "mqtt_user", settings->mqtt_user);
    cJSON_AddStringToObject(root, "mqtt_pass", settings->mqtt_pass);
    cJSON_AddStringToObject(root, "device_name", settings->device_name);

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to print JSON");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    FILE *f = fopen(SETTINGS_FILE_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open settings file for writing");
        free(json_str);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    fwrite(json_str, 1, strlen(json_str), f);
    fclose(f);
    free(json_str);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Settings saved");
    return ESP_OK;
}

// Load settings from JSON file (fills the provided settings struct)
esp_err_t load_settings(device_settings_t *settings) {
    FILE *f = fopen(SETTINGS_FILE_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "Settings file not found, using defaults");
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc(size + 1);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON");
        fclose(f);
        return ESP_FAIL;
    }

    fread(json_str, 1, size, f);
    json_str[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }

    cJSON *temp;

    temp = cJSON_GetObjectItemCaseSensitive(root, "ip_address");
    if (cJSON_IsString(temp) && (temp->valuestring != NULL)) {
        strncpy(settings->ip_address, temp->valuestring, sizeof(settings->ip_address));
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "gateway");
    if (cJSON_IsString(temp) && (temp->valuestring != NULL)) {
        strncpy(settings->gateway, temp->valuestring, sizeof(settings->gateway));
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "netmask");
    if (cJSON_IsString(temp) && (temp->valuestring != NULL)) {
        strncpy(settings->netmask, temp->valuestring, sizeof(settings->netmask));
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "dhcp_enable");
    if (cJSON_IsBool(temp)) {
        settings->dhcp_enable = cJSON_IsTrue(temp);
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "dir_invert");
    if (cJSON_IsBool(temp)) {
        settings->dir_invert = cJSON_IsTrue(temp);
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "roller_limit");
    if (cJSON_IsNumber(temp)) {
        settings->roller_limit = temp->valueint;
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "roller_pos");
    if (cJSON_IsNumber(temp)) {
        settings->roller_pos = temp->valueint;
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "max_speed");
    if (cJSON_IsNumber(temp)) {
        settings->max_speed = temp->valueint;
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "mqtt_uri");
    if (cJSON_IsString(temp) && (temp->valuestring != NULL)) {
        strncpy(settings->mqtt_uri, temp->valuestring, sizeof(settings->mqtt_uri));
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "mqtt_user");
    if (cJSON_IsString(temp) && (temp->valuestring != NULL)) {
        strncpy(settings->mqtt_user, temp->valuestring, sizeof(settings->mqtt_user));
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "mqtt_pass");
    if (cJSON_IsString(temp) && (temp->valuestring != NULL)) {
        strncpy(settings->mqtt_pass, temp->valuestring, sizeof(settings->mqtt_pass));
    }

    temp = cJSON_GetObjectItemCaseSensitive(root, "device_name");
    if (cJSON_IsString(temp) && (temp->valuestring != NULL)) {
        strncpy(settings->device_name, temp->valuestring, sizeof(settings->device_name));
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Settings loaded");
    return ESP_OK;
}

void print_settings(const device_settings_t *settings) {
    ESP_LOGI("Settings", "Device Settings:");
    ESP_LOGI("Settings", "  IP Address   : %s", settings->ip_address);
    ESP_LOGI("Settings", "  Gateway      : %s", settings->gateway);
    ESP_LOGI("Settings", "  Netmask      : %s", settings->netmask);
    ESP_LOGI("Settings", "  DHCP Enable  : %s", settings->dhcp_enable ? "true" : "false");
    ESP_LOGI("Settings", "  Dir Invert   : %s", settings->dir_invert ? "true" : "false");
    ESP_LOGI("Settings", "  Roller Limit : %d", settings->roller_limit);
    ESP_LOGI("Settings", "  Roller Pos   : %d", settings->roller_pos);
    ESP_LOGI("Settings", "  Max Speed    : %d", settings->max_speed);
    ESP_LOGI("Settings", "  MQTT URI     : %s", settings->mqtt_uri);
    ESP_LOGI("Settings", "  MQTT User    : %s", settings->mqtt_user);
    ESP_LOGI("Settings", "  MQTT Pass    : %s", settings->mqtt_pass);
    ESP_LOGI("Settings", "  Device Name  : %s", settings->device_name);
}
