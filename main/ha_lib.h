#ifndef __HA_LIB_H__
#define __HA_LIB_H__

#include <stdint.h>

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
    char unique_id[64];
    void *config_struct;
    uint8_t type;
} subscribe_buffer_t;

void ha_lib_init(char *mqtt_uri, char *mqtt_user, char *mqtt_pass);

uint8_t ha_lib_mqtt_connected(void);

subscribe_buffer_t *ha_lib_cover_register(ha_cover_param_t *param);

subscribe_buffer_t *ha_lib_switch_register(ha_switch_param_t *param);

subscribe_buffer_t *ha_lib_button_register(ha_button_param_t *param);

subscribe_buffer_t *ha_lib_text_register(ha_text_param_t *param);

subscribe_buffer_t *ha_lib_number_register(ha_number_param_t *param);

void ha_lib_cover_set_state(subscribe_buffer_t *sub_buffer, char *state);

void ha_lib_cover_set_position(subscribe_buffer_t *sub_buffer, uint8_t position);

void ha_lib_switch_update(subscribe_buffer_t *sensor_buffer, const char *new_state);

void ha_lib_text_sensor_update(subscribe_buffer_t *sensor_buffer, const char *new_state);

void ha_lib_number_update(subscribe_buffer_t *number_buffer, int number);

#endif