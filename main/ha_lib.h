#ifndef __HA_LIB_H__
#define __HA_LIB_H__

#include <stdint.h>

typedef enum {
    HA_COMPONENT_COVER = 0x03,
    HA_COMPONENT_SWITCH = 0x04,
    HA_COMPONENT_BUTTON = 0x05,
    HA_COMPONENT_TEXT_SENSOR = 0x06,
    HA_COMPONENT_NUMBER = 0x07,
    HA_COMPONENT_LIGHT = 0x08
} ha_component_type_t;

typedef struct {
    char name[128];
    char *device_name;
    char *manufacturer;
    char *model;
    char *identifiers;
    char *sw_version;

    void (*update_mqtt)(char *, char *, int);
} ha_cover_param_t;

typedef struct {
    char name[128];
    char *device_name;
    const char *manufacturer;
    const char *model;
    const char *identifiers;
    const char *sw_version;

    void (*update_mqtt)(char *, char *, int);
} ha_switch_param_t;

typedef struct {
    char name[128];
    char *device_name;
    const char *manufacturer;
    const char *model;
    const char *identifiers;
    const char *sw_version;

    void (*update_mqtt)(char *, char *, int);
} ha_button_param_t;

typedef struct {
    char name[128];
    char *device_name;
    const char *manufacturer;
    const char *model;
    const char *identifiers;
    const char *sw_version;
} ha_text_param_t;

typedef struct {
    char name[128];
    char *device_name;
    const char *manufacturer;
    const char *model;
    const char *identifiers;
    const char *sw_version;
    int min_value;
    int max_value;
    int step;

    void (*update_mqtt)(char *, char *, int);
} ha_number_param_t;

typedef struct {
    char name[128];
    char *device_name;
    const char *manufacturer;
    const char *model;
    const char *identifiers;
    const char *sw_version;
    uint8_t support_brightness;
    uint8_t support_color_temp;
    uint8_t support_rgb;
    uint16_t min_mireds;
    uint16_t max_mireds;

    void (*update_mqtt)(char *, char *, int);
} ha_light_param_t;

typedef struct {
    char unique_id[64];
    void *config_struct;
    ha_component_type_t type;
} subscribe_buffer_t;

void ha_lib_init(char *mqtt_uri, char *mqtt_user, char *mqtt_pass);

uint8_t ha_lib_mqtt_connected(void);

subscribe_buffer_t *ha_lib_cover_register(ha_cover_param_t *param);

subscribe_buffer_t *ha_lib_switch_register(ha_switch_param_t *param);

subscribe_buffer_t *ha_lib_button_register(ha_button_param_t *param);

subscribe_buffer_t *ha_lib_text_register(ha_text_param_t *param);

subscribe_buffer_t *ha_lib_number_register(ha_number_param_t *param);

subscribe_buffer_t *ha_lib_light_register(ha_light_param_t *param);

void ha_lib_cover_set_state(subscribe_buffer_t *sub_buffer, char *state);

void ha_lib_cover_set_position(subscribe_buffer_t *sub_buffer, uint8_t position);

void ha_lib_switch_update(subscribe_buffer_t *sensor_buffer, const char *new_state);

void ha_lib_text_sensor_update(subscribe_buffer_t *sensor_buffer, const char *new_state);

void ha_lib_number_update(subscribe_buffer_t *number_buffer, int number);

void ha_lib_light_set_state(subscribe_buffer_t *light_buffer, const char *state);

void ha_lib_light_set_brightness(subscribe_buffer_t *light_buffer, uint8_t brightness);

void ha_lib_light_set_color_rgb(subscribe_buffer_t *light_buffer, uint8_t r, uint8_t g, uint8_t b);

void ha_lib_light_set_color_temp(subscribe_buffer_t *light_buffer, uint16_t color_temp);

void ha_lib_light_set_full_state(subscribe_buffer_t *light_buffer, const char *state, uint8_t brightness, uint8_t r, uint8_t g, uint8_t b, uint16_t color_temp);

#endif