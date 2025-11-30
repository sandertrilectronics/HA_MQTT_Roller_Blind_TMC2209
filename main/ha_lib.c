#include "ha_lib.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "secret.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static uint8_t _ha_mqtt_connected = 0;
static char _ha_lib_id[32] = {0};
static TimerHandle_t availability_timer = NULL;
static esp_mqtt_client_handle_t client = NULL;

static const char *discover_packet_cover = "{"
"\"name\": \"%s\","
"\"cmd_t\": \"cover/%s/set\","
"\"set_pos_t\": \"cover/%s/set\","
"\"stat_t\": \"cover/%s/state\","
"\"val_tpl\": \"{{ value_json.state }}\","
"\"pos_t\": \"cover/%s/state\","
"\"pos_tpl\": \"{{ value_json.position }}\","
"\"avty_t\": \"cover/%s/availability\","
"\"qos\": 0,"
"\"ret\": true,"
"\"pl_open\": \"OPEN\","
"\"pl_cls\": \"CLOSE\","
"\"pl_stop\": \"STOP\","
"\"stat_open\": \"open\","
"\"stat_opening\": \"opening\","
"\"stat_clsd\": \"close\","
"\"stat_closing\": \"closing\","
"\"uniq_id\": \"%s\","
"\"platform\": \"cover\","
"\"pos_clsd\": 100,"
"\"pos_open\": 0,"
"\"device\": {"
"\"name\": \"%s\","
"\"manufacturer\": \"%s\","
"\"model\": \"%s\","
"\"identifiers\": \"%s_%s\","
"\"sw_version\": \"%s\""
"}"
"}";

static const char *discover_packet_switch = "{"
"\"name\": \"%s\","
"\"cmd_t\": \"switch/%s/set\","
"\"stat_t\": \"switch/%s/state\","
"\"avty_t\": \"switch/%s/availability\","
"\"uniq_id\": \"%s\","
"\"platform\": \"switch\","
"\"device\": {"
"\"name\": \"%s\","
"\"manufacturer\": \"%s\","
"\"model\": \"%s\","
"\"identifiers\": \"%s_%s\","
"\"sw_version\": \"%s\""
"}"
"}";

static const char *discover_packet_button = "{"
"\"name\": \"%s\","
"\"unique_id\": \"%s\","
"\"cmd_t\": \"button/%s/press\","
"\"avty_t\": \"button/%s/availability\","
"\"platform\": \"button\","
"\"device\": {"
"\"name\": \"%s\","
"\"manufacturer\": \"%s\","
"\"model\": \"%s\","
"\"identifiers\": \"%s_%s\","
"\"sw_version\": \"%s\""
"}"
"}";

static const char *discover_packet_text_sensor = "{"
"\"name\": \"%s\","
"\"uniq_id\": \"%s\","
"\"cmd_t \": \"sensor/%s/state\","
"\"avty_t\": \"sensor/%s/availability\","
"\"platform\": \"text\","
"\"device\": {"
"\"name\": \"%s\","
"\"manufacturer\": \"%s\","
"\"model\": \"%s\","
"\"identifiers\": \"%s_%s\","
"\"sw_version\": \"%s\""
"}"
"}";

static const char *discover_packet_number = "{"
"\"name\": \"%s\","
"\"uniq_id\": \"%s\","
"\"cmd_t\": \"number/%s/set\","
"\"stat_t\": \"number/%s/state\","
"\"min\": %d,"
"\"max\": %d,"
"\"step\": %d,"
"\"unit_of_measurement\": \"RPM\","
"\"avty_t\": \"number/%s/availability\","
"\"platform\": \"number\","
"\"device\": {"
"\"name\": \"%s\","
"\"manufacturer\": \"%s\","
"\"model\": \"%s\","
"\"identifiers\": \"%s_%s\","
"\"sw_version\": \"%s\""
"}"
"}";

static const char *discover_packet_light = "{"
"\"name\": \"%s\","
"\"uniq_id\": \"%s\","
"\"cmd_t\": \"light/%s/set\","
"\"stat_t\": \"light/%s/state\","
"\"avty_t\": \"light/%s/availability\","
"\"brightness\": %s,"
"\"color_temp\": %s,"
"\"rgb\": %s,"
"\"min_mireds\": %d,"
"\"max_mireds\": %d,"
"\"schema\": \"json\","
"\"platform\": \"light\","
"\"device\": {"
"\"name\": \"%s\","
"\"manufacturer\": \"%s\","
"\"model\": \"%s\","
"\"identifiers\": \"%s_%s\","
"\"sw_version\": \"%s\""
"}"
"}";

static void _create_unique_id(char *buffer, uint16_t buffer_len, char *prepend) {
    static uint8_t increment = 0;

    // get mac address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // fill static setings in air quality data (aqd)
    snprintf(buffer, buffer_len, "%s_%02x%02x%02x%02x%02x%02x%02x", prepend, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], increment);

    // randomize
    increment++;
}

static void _create_id(char *buffer, uint16_t buffer_len) {
    static uint8_t increment = 0;

    // get mac address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // fill static setings in air quality data (aqd)
    snprintf(buffer, buffer_len, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // randomize
    increment++;
}

static void _create_discover_packet_cover(char *buffer, uint16_t buffer_len, ha_cover_param_t *param, char *unique_id) {
    snprintf(buffer, buffer_len, discover_packet_cover, 
        param->name,
        unique_id,
        unique_id,
        unique_id,
        unique_id,
        unique_id,
        unique_id,
        param->device_name,
        param->manufacturer,
        param->model,
        param->identifiers,
        _ha_lib_id,
        param->sw_version
    );
}

static void _create_discover_packet_switch(char *buffer, uint16_t buffer_len, ha_switch_param_t *param, char *unique_id) {
    snprintf(buffer, buffer_len, discover_packet_switch, 
        param->name,
        unique_id,
        unique_id,
        unique_id,
        unique_id,
        param->device_name,
        param->manufacturer,
        param->model,
        param->identifiers,
        _ha_lib_id,
        param->sw_version
    );
}

static void _create_discover_packet_button(char *buffer, uint16_t buffer_len, ha_button_param_t *param, char *unique_id) {
    snprintf(buffer, buffer_len, discover_packet_button,
        param->name,
        unique_id,
        unique_id,
        unique_id,
        param->device_name,
        param->manufacturer,
        param->model,
        param->identifiers,
        _ha_lib_id,
        param->sw_version
    );
}

static void _create_discover_packet_text_sensor(char *buffer, uint16_t buffer_len, ha_text_param_t *param, char *unique_id) {
    snprintf(buffer, buffer_len, discover_packet_text_sensor,
        param->name,
        unique_id,
        unique_id,
        unique_id,
        param->device_name,
        param->manufacturer,
        param->model,
        param->identifiers,
        _ha_lib_id,
        param->sw_version
    );
}

static void _create_discover_packet_number(char *buffer, uint16_t buffer_len, ha_number_param_t *param, char *unique_id) {
    snprintf(buffer, buffer_len, discover_packet_number,
        param->name,
        unique_id,
        unique_id,
        unique_id,
        param->min_value,
        param->max_value,
        param->step,
        unique_id,
        param->device_name,
        param->manufacturer,
        param->model,
        param->identifiers,
        _ha_lib_id,
        param->sw_version
    );
}

static void _create_discover_packet_light(char *buffer, uint16_t buffer_len, ha_light_param_t *param, char *unique_id) {
    snprintf(buffer, buffer_len, discover_packet_light,
        param->name,
        unique_id,
        unique_id,
        unique_id,
        unique_id,
        param->support_brightness ? "true" : "false",
        param->support_color_temp ? "true" : "false", 
        param->support_rgb ? "true" : "false",
        param->min_mireds,
        param->max_mireds,
        param->device_name,
        param->manufacturer,
        param->model,
        param->identifiers,
        _ha_lib_id,
        param->sw_version
    );
}

static uint8_t _subscribe_buffer_index = 0;
static subscribe_buffer_t _subscribe_buffer[8] = {0};

static void availability_timer_callback(TimerHandle_t xTimer) {
    if (!_ha_mqtt_connected) {
        return;
    }
    
    char buffer[256];
    
    // Publish availability for all registered components
    for (uint8_t i = 0; i < _subscribe_buffer_index; i++) {
        switch (_subscribe_buffer[i].type) {
            case HA_COMPONENT_COVER:
                snprintf(buffer, sizeof(buffer), "cover/%s/availability", _subscribe_buffer[i].unique_id);
                esp_mqtt_client_publish(client, buffer, "online", 0, 1, 0);
                break;
                
            case HA_COMPONENT_SWITCH:
                snprintf(buffer, sizeof(buffer), "switch/%s/availability", _subscribe_buffer[i].unique_id);
                esp_mqtt_client_publish(client, buffer, "online", 0, 1, 0);
                break;
                
            case HA_COMPONENT_BUTTON:
                snprintf(buffer, sizeof(buffer), "button/%s/availability", _subscribe_buffer[i].unique_id);
                esp_mqtt_client_publish(client, buffer, "online", 0, 1, 0);
                break;
                
            case HA_COMPONENT_TEXT_SENSOR:
                snprintf(buffer, sizeof(buffer), "sensor/%s/availability", _subscribe_buffer[i].unique_id);
                esp_mqtt_client_publish(client, buffer, "online", 0, 1, 0);
                // Also set initial state for text sensor
                snprintf(buffer, sizeof(buffer), "sensor/%s/state", _subscribe_buffer[i].unique_id);
                esp_mqtt_client_publish(client, buffer, "Idle State", 0, 1, 0);
                break;
                
            case HA_COMPONENT_NUMBER:
                snprintf(buffer, sizeof(buffer), "number/%s/availability", _subscribe_buffer[i].unique_id);
                esp_mqtt_client_publish(client, buffer, "online", 0, 1, 0);
                break;
                
            case HA_COMPONENT_LIGHT:
                snprintf(buffer, sizeof(buffer), "light/%s/availability", _subscribe_buffer[i].unique_id);
                esp_mqtt_client_publish(client, buffer, "online", 0, 1, 0);
                break;
        }
    }

    _ha_mqtt_connected = 2;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD("MQTT", "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    char buffer[256];
    char discover_packet[1024];
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI("MQTT", "MQTT_EVENT_CONNECTED");
    
        for (uint8_t i = 0; i < _subscribe_buffer_index; i++) {
            switch (_subscribe_buffer[i].type) {
                case HA_COMPONENT_COVER:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "cover/%s/set", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);
                    //snprintf(buffer, sizeof(buffer), "cover/%s/state", _subscribe_buffer[i].unique_id);
                    //esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/cover/%s/%s/config", _ha_lib_id, _subscribe_buffer[i].unique_id);
                    _create_discover_packet_cover(discover_packet, sizeof(discover_packet), (ha_cover_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);
                    break;
    
                case HA_COMPONENT_SWITCH:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "switch/%s/set", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);
                    //snprintf(buffer, sizeof(buffer), "switch/%s/state", _subscribe_buffer[i].unique_id);
                    //esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/switch/%s/%s/config", _ha_lib_id, _subscribe_buffer[i].unique_id);
                    _create_discover_packet_switch(discover_packet, sizeof(discover_packet), (ha_switch_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);
                    break;
    
                case HA_COMPONENT_BUTTON:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "button/%s/press", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/button/%s/%s/config", _ha_lib_id, _subscribe_buffer[i].unique_id);
                    _create_discover_packet_button(discover_packet, sizeof(discover_packet), (ha_button_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);
                    break;

                case HA_COMPONENT_TEXT_SENSOR:
                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/sensor/%s/%s/config", _ha_lib_id, _subscribe_buffer[i].unique_id);
                    _create_discover_packet_text_sensor(discover_packet, sizeof(discover_packet), (ha_text_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);
                    break;

                case HA_COMPONENT_NUMBER:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "number/%s/set", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/number/%s/%s/config", _ha_lib_id, _subscribe_buffer[i].unique_id);
                    _create_discover_packet_number(discover_packet, sizeof(discover_packet), (ha_number_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);
                    break;

                case HA_COMPONENT_LIGHT:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "light/%s/set", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/light/%s/%s/config", _ha_lib_id, _subscribe_buffer[i].unique_id);
                    _create_discover_packet_light(discover_packet, sizeof(discover_packet), (ha_light_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);
                    break;
            }
        }

        _ha_mqtt_connected = 1;
        
        // Start availability timer to publish availability status after 1 second
        if (availability_timer != NULL) {
            xTimerStart(availability_timer, 0);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI("MQTT", "MQTT_EVENT_DISCONNECTED");
        _ha_mqtt_connected = 0;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI("MQTT", "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI("MQTT", "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI("MQTT", "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI("MQTT", "MQTT_EVENT_DATA");
        //ESP_LOGI("MQTT", "%.*s %.*s", event->topic_len, event->topic, event->data_len, event->data);
        for (uint8_t i = 0; i < _subscribe_buffer_index; i++) {
            switch (_subscribe_buffer[i].type) {
                case HA_COMPONENT_COVER:
                    snprintf(buffer, sizeof(buffer), "cover/%s/set", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_cover_param_t *struct_cover = (ha_cover_param_t *)_subscribe_buffer[i].config_struct;
                        struct_cover->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

                    snprintf(buffer, sizeof(buffer), "cover/%s/state", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_cover_param_t *struct_cover = (ha_cover_param_t *)_subscribe_buffer[i].config_struct;
                        struct_cover->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

                    break;
                
                case HA_COMPONENT_SWITCH:
                    snprintf(buffer, sizeof(buffer), "switch/%s/set", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_switch_param_t *struct_cover = (ha_switch_param_t *)_subscribe_buffer[i].config_struct;
                        struct_cover->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

                    snprintf(buffer, sizeof(buffer), "switch/%s/state", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_switch_param_t *struct_cover = (ha_switch_param_t *)_subscribe_buffer[i].config_struct;
                        struct_cover->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

                    break;
                
                case HA_COMPONENT_BUTTON:
                    snprintf(buffer, sizeof(buffer), "button/%s/press", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_button_param_t *struct_cover = (ha_button_param_t *)_subscribe_buffer[i].config_struct;
                        struct_cover->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

                    break;
                
                case HA_COMPONENT_NUMBER:
                    snprintf(buffer, sizeof(buffer), "number/%s/set", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_number_param_t *struct_cover = (ha_number_param_t *)_subscribe_buffer[i].config_struct;
                        struct_cover->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

                    break;

                case HA_COMPONENT_LIGHT:
                    snprintf(buffer, sizeof(buffer), "light/%s/set", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_light_param_t *struct_light = (ha_light_param_t *)_subscribe_buffer[i].config_struct;
                        struct_light->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

                    break;

                default:
                    break;
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI("MQTT", "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGI("MQTT", "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI("MQTT", "Other event id:%d", event->event_id);
        break;
    }
}

void ha_lib_init(char *mqtt_uri, char *mqtt_user, char *mqtt_pass) {
    // configure client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_uri,
        .credentials.username = mqtt_user,
        .credentials.authentication.password = mqtt_pass
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    
    // create availability timer (one-shot, 1 second)
    availability_timer = xTimerCreate(
        "availability_timer",
        pdMS_TO_TICKS(1000),  // 1 second
        pdFALSE,              // one-shot timer
        NULL,                 // timer ID
        availability_timer_callback
    );
    
    // start mqtt client
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    // create device id
    _create_id(_ha_lib_id, sizeof(_ha_lib_id));
}

uint8_t ha_lib_mqtt_connected(void) {
    return (_ha_mqtt_connected == 2) ? 1 : 0;
}

subscribe_buffer_t *ha_lib_cover_register(ha_cover_param_t *param) {
    if (_subscribe_buffer_index >= 8)
        return NULL;

    _create_unique_id(_subscribe_buffer[_subscribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subscribe_buffer_index].unique_id), "cover");
    _subscribe_buffer[_subscribe_buffer_index].type = HA_COMPONENT_COVER;
    _subscribe_buffer[_subscribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subscribe_buffer_index];
    _subscribe_buffer_index++;

    return ptr;
}

// For Switch
subscribe_buffer_t *ha_lib_switch_register(ha_switch_param_t *param) {
    if (_subscribe_buffer_index >= 8)
        return NULL;

    _create_unique_id(_subscribe_buffer[_subscribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subscribe_buffer_index].unique_id), "switch");
    _subscribe_buffer[_subscribe_buffer_index].type = HA_COMPONENT_SWITCH;
    _subscribe_buffer[_subscribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subscribe_buffer_index];
    _subscribe_buffer_index++;

    return ptr;
}

// For Button
subscribe_buffer_t *ha_lib_button_register(ha_button_param_t *param) {
    if (_subscribe_buffer_index >= 8)
        return NULL;

    _create_unique_id(_subscribe_buffer[_subscribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subscribe_buffer_index].unique_id), "button");
    _subscribe_buffer[_subscribe_buffer_index].type = HA_COMPONENT_BUTTON;
    _subscribe_buffer[_subscribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subscribe_buffer_index];
    _subscribe_buffer_index++;

    return ptr;
}

//
subscribe_buffer_t *ha_lib_text_register(ha_text_param_t *param) {
    if (_subscribe_buffer_index >= 8)
        return NULL;

    _create_unique_id(_subscribe_buffer[_subscribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subscribe_buffer_index].unique_id), "text");
    _subscribe_buffer[_subscribe_buffer_index].type = HA_COMPONENT_TEXT_SENSOR;
    _subscribe_buffer[_subscribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subscribe_buffer_index];
    _subscribe_buffer_index++;

    return ptr;
}

subscribe_buffer_t *ha_lib_number_register(ha_number_param_t *param) {
    if (_subscribe_buffer_index >= 8)
        return NULL;

    _create_unique_id(_subscribe_buffer[_subscribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subscribe_buffer_index].unique_id), "number");
    _subscribe_buffer[_subscribe_buffer_index].type = HA_COMPONENT_NUMBER;
    _subscribe_buffer[_subscribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subscribe_buffer_index];
    _subscribe_buffer_index++;

    return ptr;
}

subscribe_buffer_t *ha_lib_light_register(ha_light_param_t *param) {
    if (_subscribe_buffer_index >= 8)
        return NULL;

    _create_unique_id(_subscribe_buffer[_subscribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subscribe_buffer_index].unique_id), "light");
    _subscribe_buffer[_subscribe_buffer_index].type = HA_COMPONENT_LIGHT;
    _subscribe_buffer[_subscribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subscribe_buffer_index];
    _subscribe_buffer_index++;

    return ptr;
}

void ha_lib_cover_set_state(subscribe_buffer_t *sub_buffer, char *state) {
    char buffer_topic[128];
    char buffer_msg[128];
    snprintf(buffer_topic, sizeof(buffer_topic), "cover/%s/state", sub_buffer->unique_id);
    snprintf(buffer_msg, sizeof(buffer_msg), "{\"state\":\"%s\"}", state);
    esp_mqtt_client_publish(client, (const char *)buffer_topic, (const char *)buffer_msg, 0, 1, 0);
}

void ha_lib_cover_set_position(subscribe_buffer_t *sub_buffer, uint8_t position) {
    char buffer_topic[128];
    char buffer_msg[128];
    snprintf(buffer_topic, sizeof(buffer_topic), "cover/%s/state", sub_buffer->unique_id);
    snprintf(buffer_msg, sizeof(buffer_msg), "{\"position\":%d}", position);
    esp_mqtt_client_publish(client, (const char *)buffer_topic, (const char *)buffer_msg, 0, 1, 0);
}

void ha_lib_switch_update(subscribe_buffer_t *sensor_buffer, const char *new_state) {
    char topic[128];
    snprintf(topic, sizeof(topic), "switch/%s/state", sensor_buffer->unique_id);
    esp_mqtt_client_publish(client, topic, new_state, 0, 1, 0);
}

void ha_lib_button_press(subscribe_buffer_t *button_buffer) {
    char topic[128];
    snprintf(topic, sizeof(topic), "button/%s/press", button_buffer->unique_id);
    esp_mqtt_client_publish(client, topic, "pressed", 0, 1, 0);
}

void ha_lib_text_sensor_update(subscribe_buffer_t *sensor_buffer, const char *new_state) {
    char topic[128];
    snprintf(topic, sizeof(topic), "sensor/%s/state", sensor_buffer->unique_id);
    esp_mqtt_client_publish(client, topic, new_state, 0, 1, 0);
}

void ha_lib_number_update(subscribe_buffer_t *number_buffer, int number) {
    char topic[128];
    char num_buf[128];
    snprintf(topic, sizeof(topic), "number/%s/state", number_buffer->unique_id);
    snprintf(num_buf, sizeof(num_buf), "%d", number);
    esp_mqtt_client_publish(client, topic, num_buf, 0, 1, 0);
}

void ha_lib_light_set_state(subscribe_buffer_t *light_buffer, const char *state) {
    char topic[128];
    char msg_buf[256];
    snprintf(topic, sizeof(topic), "light/%s/state", light_buffer->unique_id);
    snprintf(msg_buf, sizeof(msg_buf), "{\"state\":\"%s\"}", state);
    esp_mqtt_client_publish(client, topic, msg_buf, 0, 1, 0);
}

void ha_lib_light_set_brightness(subscribe_buffer_t *light_buffer, uint8_t brightness) {
    char topic[128];
    char msg_buf[256];
    snprintf(topic, sizeof(topic), "light/%s/state", light_buffer->unique_id);
    snprintf(msg_buf, sizeof(msg_buf), "{\"state\":\"ON\",\"brightness\":%d}", brightness);
    esp_mqtt_client_publish(client, topic, msg_buf, 0, 1, 0);
}

void ha_lib_light_set_color_rgb(subscribe_buffer_t *light_buffer, uint8_t r, uint8_t g, uint8_t b) {
    char topic[128];
    char msg_buf[256];
    snprintf(topic, sizeof(topic), "light/%s/state", light_buffer->unique_id);
    snprintf(msg_buf, sizeof(msg_buf), "{\"state\":\"ON\",\"color\":{\"r\":%d,\"g\":%d,\"b\":%d}}", r, g, b);
    esp_mqtt_client_publish(client, topic, msg_buf, 0, 1, 0);
}

void ha_lib_light_set_color_temp(subscribe_buffer_t *light_buffer, uint16_t color_temp) {
    char topic[128];
    char msg_buf[256];
    snprintf(topic, sizeof(topic), "light/%s/state", light_buffer->unique_id);
    snprintf(msg_buf, sizeof(msg_buf), "{\"state\":\"ON\",\"color_temp\":%d}", color_temp);
    esp_mqtt_client_publish(client, topic, msg_buf, 0, 1, 0);
}

void ha_lib_light_set_full_state(subscribe_buffer_t *light_buffer, const char *state, uint8_t brightness, uint8_t r, uint8_t g, uint8_t b, uint16_t color_temp) {
    char topic[128];
    char msg_buf[512];
    snprintf(topic, sizeof(topic), "light/%s/state", light_buffer->unique_id);
    snprintf(msg_buf, sizeof(msg_buf), 
        "{\"state\":\"%s\",\"brightness\":%d,\"color\":{\"r\":%d,\"g\":%d,\"b\":%d},\"color_temp\":%d}", 
        state, brightness, r, g, b, color_temp);
    esp_mqtt_client_publish(client, topic, msg_buf, 0, 1, 0);
}