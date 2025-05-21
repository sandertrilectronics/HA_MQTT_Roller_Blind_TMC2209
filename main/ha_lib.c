#include "ha_lib.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "secret.h"

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
"\"identifiers\": \"%s\","
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
"\"identifiers\": \"%s\","
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
"\"identifiers\": \"%s\","
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
"\"identifiers\": \"%s\","
"\"sw_version\": \"%s\""
"}"
"}";

static void _create_name(char *buffer, uint16_t buffer_len, char *prepend) {
    static uint8_t increment = 0;

    // get mac address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // fill static setings in air quality data (aqd)
    snprintf(buffer, buffer_len, "%s_%02x%02x%02x%02x%02x%02x%02x", prepend, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], increment);

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
        param->sw_version
    );
}

static uint8_t _subsribe_buffer_index = 0;
static subscribe_buffer_t _subscribe_buffer[8] = {0};

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD("MQTT", "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    char buffer[128];
    char discover_packet[1024];
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI("MQTT", "MQTT_EVENT_CONNECTED");
    
        for (uint8_t i = 0; i < _subsribe_buffer_index; i++) {
            switch (_subscribe_buffer[i].type) {
                case 0x03:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "cover/%s/set", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);
                    //snprintf(buffer, sizeof(buffer), "cover/%s/state", _subscribe_buffer[i].unique_id);
                    //esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/cover/%s/config", _subscribe_buffer[i].unique_id);
                    _create_discover_packet_cover(discover_packet, sizeof(discover_packet), (ha_cover_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);

                    // set state to online
                    snprintf(buffer, sizeof(buffer), "cover/%s/availability", _subscribe_buffer[i].unique_id);
                    snprintf(discover_packet, sizeof(discover_packet), "online");
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 0);
                    break;
    
                case 0x04:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "switch/%s/set", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);
                    //snprintf(buffer, sizeof(buffer), "switch/%s/state", _subscribe_buffer[i].unique_id);
                    //esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/switch/%s/config", _subscribe_buffer[i].unique_id);
                    _create_discover_packet_switch(discover_packet, sizeof(discover_packet), (ha_switch_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);

                    // set state to online
                    snprintf(buffer, sizeof(buffer), "switch/%s/availability", _subscribe_buffer[i].unique_id);
                    snprintf(discover_packet, sizeof(discover_packet), "online");
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 0);
                    break;
    
                case 0x05:
                    // subscribe to callbacks
                    snprintf(buffer, sizeof(buffer), "button/%s/press", _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_subscribe(client, buffer, 1);

                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/button/%s/config", _subscribe_buffer[i].unique_id);
                    _create_discover_packet_button(discover_packet, sizeof(discover_packet), (ha_button_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);

                    // set state to online
                    snprintf(buffer, sizeof(buffer), "button/%s/availability", _subscribe_buffer[i].unique_id);
                    snprintf(discover_packet, sizeof(discover_packet), "online");
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 0);
                    break;

                case 0x06:
                    // publish discover packet
                    snprintf(buffer, sizeof(buffer), "homeassistant/sensor/%s/config", _subscribe_buffer[i].unique_id);
                    _create_discover_packet_text_sensor(discover_packet, sizeof(discover_packet), (ha_text_param_t *)_subscribe_buffer[i].config_struct, _subscribe_buffer[i].unique_id);
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 1);

                    // set state to online
                    snprintf(buffer, sizeof(buffer), "sensor/%s/availability", _subscribe_buffer[i].unique_id);
                    snprintf(discover_packet, sizeof(discover_packet), "online");
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 0);

                    // set state to online
                    snprintf(buffer, sizeof(buffer), "sensor/%s/state", _subscribe_buffer[i].unique_id);
                    snprintf(discover_packet, sizeof(discover_packet), "Idle State");
                    esp_mqtt_client_publish(client, buffer, discover_packet, 0, 1, 0);
                    break;
            }
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI("MQTT", "MQTT_EVENT_DISCONNECTED");
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
        for (uint8_t i = 0; i < _subsribe_buffer_index; i++) {
            switch (_subscribe_buffer[i].type) {
                case 0x03:
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
                
                case 0x04:
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
                
                case 0x05:
                    snprintf(buffer, sizeof(buffer), "button/%s/press", _subscribe_buffer[i].unique_id);
                    if (memcmp(event->topic, buffer, event->topic_len) == 0) {
                        ha_button_param_t *struct_cover = (ha_button_param_t *)_subscribe_buffer[i].config_struct;
                        struct_cover->update_mqtt(event->topic, event->data, event->data_len);
                        break;
                    }

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

static esp_mqtt_client_handle_t client = NULL;

void ha_lib_init(char *mqtt_uri, char *mqtt_user, char *mqtt_pass) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_uri,
        .credentials.username = mqtt_user,
        .credentials.authentication.password = mqtt_pass
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

subscribe_buffer_t *ha_lib_cover_register(ha_cover_param_t *param) {
    if (_subsribe_buffer_index >= 8)
        return NULL;

    _create_name(_subscribe_buffer[_subsribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subsribe_buffer_index].unique_id), "cover");
    _subscribe_buffer[_subsribe_buffer_index].type = 0x03;
    _subscribe_buffer[_subsribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subsribe_buffer_index];
    _subsribe_buffer_index++;

    return ptr;
}

// For Switch
subscribe_buffer_t *ha_lib_switch_register(ha_switch_param_t *param) {
    if (_subsribe_buffer_index >= 8)
        return NULL;

    _create_name(_subscribe_buffer[_subsribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subsribe_buffer_index].unique_id), "switch");
    _subscribe_buffer[_subsribe_buffer_index].type = 0x04;
    _subscribe_buffer[_subsribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subsribe_buffer_index];
    _subsribe_buffer_index++;

    return ptr;
}

// For Button
subscribe_buffer_t *ha_lib_button_register(ha_button_param_t *param) {
    if (_subsribe_buffer_index >= 8)
        return NULL;

    _create_name(_subscribe_buffer[_subsribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subsribe_buffer_index].unique_id), "button");
    _subscribe_buffer[_subsribe_buffer_index].type = 0x05;
    _subscribe_buffer[_subsribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subsribe_buffer_index];
    _subsribe_buffer_index++;

    return ptr;
}

//
subscribe_buffer_t *ha_lib_text_register(ha_text_param_t *param) {
    if (_subsribe_buffer_index >= 8)
        return NULL;

    _create_name(_subscribe_buffer[_subsribe_buffer_index].unique_id, sizeof(_subscribe_buffer[_subsribe_buffer_index].unique_id), "text");
    _subscribe_buffer[_subsribe_buffer_index].type = 0x06;
    _subscribe_buffer[_subsribe_buffer_index].config_struct = (void *)param;
    subscribe_buffer_t *ptr = &_subscribe_buffer[_subsribe_buffer_index];
    _subsribe_buffer_index++;

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