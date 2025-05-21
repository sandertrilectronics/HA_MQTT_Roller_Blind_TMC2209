#include <stdio.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <math.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "mqtt_client.h"

#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "driver/gptimer.h"
#include "esp_netif.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lwip/ip_addr.h"

#include "ha_lib.h"
#include "stp_drv.h"
#include "secret.h"
#include "sys_cfg.h"
#include "http_srv.h"

/////////////////////////////////////////////////////////////////////////////
static char s_ip_addr_str[16] = "0.0.0.0";
static bool s_handle_event_got_ip_address = false;
static bool s_handle_event_got_ip_address_mqtt = false;
static bool s_handle_event_disconnected = false;

//-----------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_CONNECTED:
        printf("Connected to the AP\n");
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        printf("Disconnected. Connecting to the AP again...\n");
        esp_wifi_connect();
        s_handle_event_disconnected = true;
        break;

    default:
        printf("Unhandled WIFI_EVENT event: %d\n", (int)event_id);
        break;
    }

    fflush(stdout);
}

//-----------------------------------------------------------------------------
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case IP_EVENT_STA_GOT_IP:
        // Note, I had to disable an ARP check on LWIP:
        // menuconfig -> Component config -> LWIP -> DISABLE 'DHCP: Perform ARP check on any offered address'
        // https://www.esp32.com/viewtopic.php?t=12859
        s_handle_event_got_ip_address = true;
        s_handle_event_got_ip_address_mqtt = true;
        break;

    default:
        printf("Unhandled IP_EVENT event: %d\n", (int)event_id);
        break;
    }
}

static void wifi_task(void *Param)
{
    printf("Wifi & OTA task starting!\n");

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            // Validate image some how, then call:
            esp_ota_mark_app_valid_cancel_rollback();
            // If needed: esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }

    httpd_handle_t server = NULL;

    esp_netif_init();

    // Register our event handler for Wi-Fi, IP and Provisioning related events
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL);

    // Initialize Wi-Fi including netif with default config
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Start Wi-Fi station
    esp_wifi_set_mode(WIFI_MODE_STA);

    if (settings.dhcp_enable == 0) {
        esp_netif_dhcpc_stop(netif);

        esp_netif_ip_info_t ip = { 0 };
        ip4addr_aton(settings.ip_address, &ip.ip);
        ip4addr_aton(settings.netmask, &ip.netmask);
        ip4addr_aton(settings.gateway, &ip.gw);

        esp_netif_set_ip_info(netif, &ip);
    }

    // build config with ssid and password
    wifi_config_t wifi_config = {0};

    // copy ssid and password
    snprintf((char *)wifi_config.sta.ssid, 32, WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, 64, WIFI_PASS);

    // configure wifi
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    esp_wifi_start();

    const uint32_t task_delay_ms = 10;

    while (1)
    {
        if (s_handle_event_got_ip_address)
        {
            s_handle_event_got_ip_address = false;
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(IP_EVENT_STA_GOT_IP, &ip_info);
            printf("My IP: " IPSTR "\n", IP2STR(&ip_info.ip));

            printf("Got IP Address - %s\n", s_ip_addr_str);
            if (server == NULL)
            {
                printf("Starting webserver\n");
                server = start_webserver();
            }
        }

        if (s_handle_event_disconnected)
        {
            s_handle_event_disconnected = false;

            if (server)
            {
                printf("Stopping webserver\n");
                httpd_stop(server);
                server = NULL;
            }
        }

        vTaskDelay(task_delay_ms / portTICK_PERIOD_MS);
    }
}

/////////////////////////////////////////////////////////////////////////////
tmc2209_io_t stepper = {
    //
    .enable = 2, //
    .tx = 27,    //
    .rx = 26,    //
    .dir = 18,   //
    .step = 23,  //
    .spread = 4  //
};

static int _atoi_checked(const char *str, int *ret)
{
    int sign = 1;
    int base = 0;
    int i = 0;

    // if whitespaces then ignore.
    while (str[i] == ' ')
    {
        i++;
    }

    // sign of number
    if (str[i] == '-' || str[i] == '+')
    {
        sign = 1 - 2 * (str[i++] == '-');
    }

    // checking for valid input
    while (str[i] != 0)
    {
        // not a number input?
        if (str[i] < '0' && str[i] > '9')
            return -1;

        // handling overflow test case
        if (base > INT_MAX / 10 || (base == INT_MAX / 10 && str[i] - '0' > 7))
        {
            if (sign == 1)
                return INT_MAX;
            else
                return INT_MIN;
        }
        base = 10 * base + (str[i++] - '0');
    }

    // set return value
    *ret = base * sign;

    // all good
    return 0;
}

//////////////////////////////////
uint8_t set_cover_state = 0;
uint8_t set_switch_mount = 0;
uint8_t set_switch_setup = 0;
uint8_t set_button_setup = 0;

void ha_cb_cover_update(char *topic, char *data, int data_len)
{
    int pos = 0;
    char buffer[data_len + 1];
    memset(buffer, 0, data_len + 1);
    memcpy(buffer, data, data_len);

    if (strcmp(buffer, "OPEN") == 0)
    {
        set_cover_state = 1;
    }
    else if (strcmp(buffer, "CLOSE") == 0)
    {
        set_cover_state = 2;
    }
    else if (strcmp(buffer, "STOP") == 0)
    {
        set_cover_state = 3;
    }
    else if (_atoi_checked(buffer, &pos) == 0)
    {
        if (pos >= 0 && pos <= 100)
        {
            set_cover_state = 100 + pos;
        }
    }
}

void ha_cb_switch_mount(char *topic, char *data, int data_len)
{
    if (memcmp(data, "OFF", data_len) == 0)
    {
        set_switch_mount = 1;
    }
    if (memcmp(data, "ON", data_len) == 0)
    {
        set_switch_mount = 2;
    }
}

void ha_cb_switch_setup(char *topic, char *data, int data_len)
{
    if (memcmp(data, "OFF", data_len) == 0)
    {
        set_switch_setup = 1;
    }
    if (memcmp(data, "ON", data_len) == 0)
    {
        set_switch_setup = 2;
    }
}

void ha_cb_button_setup(char *topic, char *data, int data_len)
{
    if (memcmp(data, "PRESS", data_len) == 0)
    {
        set_button_setup = 1;
    }
}

void ha_cb_number_rpm(char *topic, char *data, int data_len)
{
    int rpm;
    char buffer[data_len + 1];
    memset(buffer, 0, data_len + 1);
    memcpy(buffer, data, data_len);
    if (_atoi_checked(buffer, &rpm) == 0)
    {
        if (rpm >= 30 && rpm <= 300)
        {
            settings.max_speed = rpm;
        }
    }
}

ha_cover_param_t ha_cover = {
    .name = "",
    .device_name = "",
    .manufacturer = "Sander",
    .model = "RBS1",
    .identifiers = "RBS1",
    .sw_version = "1.0",
    .update_mqtt = ha_cb_cover_update};

ha_switch_param_t ha_switch_mount = {
    .name = "",
    .device_name = "",
    .manufacturer = "Sander",
    .model = "RBS1",
    .identifiers = "RBS1",
    .sw_version = "1.0",
    .update_mqtt = ha_cb_switch_mount};

ha_switch_param_t ha_switch_setup_enable = {
    .name = "",
    .device_name = "",
    .manufacturer = "Sander",
    .model = "RBS1",
    .identifiers = "RBS1",
    .sw_version = "1.0",
    .update_mqtt = ha_cb_switch_setup};

ha_button_param_t ha_button_setup = {
    .name = "",
    .device_name = "",
    .manufacturer = "Sander",
    .model = "RBS1",
    .identifiers = "RBS1",
    .sw_version = "1.0",
    .update_mqtt = ha_cb_button_setup};

ha_text_param_t ha_text_status = {
    .name = "",
    .device_name = "",
    .manufacturer = "Sander",
    .model = "RBS1",
    .identifiers = "RBS1",
    .sw_version = "1.0"
};

ha_number_param_t ha_rpm_max = {
    .name = "",
    .device_name = "",
    .manufacturer = "Sander",
    .model = "RBS1",
    .identifiers = "RBS1",
    .sw_version = "1.0",
    .min_value = 30,
    .max_value = 300,
    .step = 1,
    .update_mqtt = ha_cb_number_rpm
};
///////////////////////////////////////

void app_main(void)
{
    ESP_LOGI("SYS", "Starting...");

    {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = ((1ULL << stepper.enable) | (1ULL << stepper.spread) | (1ULL << stepper.step) | (1ULL << stepper.dir));
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 0;
        gpio_config(&io_conf);
    }

    gpio_set_level(stepper.enable, 0);
    gpio_set_level(stepper.spread, 0);

    // initialize stepper
    stepper_init(&stepper);

    // Initialize NVS.
    esp_err_t error = nvs_flash_init();
    if ((error == ESP_ERR_NVS_NO_FREE_PAGES) || (error == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        // Don't bother checking return codes, it's not like we can do anything about failures here anyways
        nvs_flash_erase();
        nvs_flash_init();
    }

    // init spiffs
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE("APP", "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE("APP", "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE("APP", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE("APP", "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI("APP", "Partition size: total: %d, used: %d", total, used);
    }

    // init settings
    load_settings(&settings);
    print_settings(&settings);

    // create defualt event loop
    esp_event_loop_create_default();

    // Put all the wifi stuff in a separate task so that we don't have to wait for a connection
    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 0, NULL);

    // wait for connection
    while (!s_handle_event_got_ip_address_mqtt)
    {
        vTaskDelay(50);
    }

    // restore position on boot
    stepper_set_position(&stepper, settings.roller_pos);

    // create names
    ha_cover.device_name = settings.device_name;
    snprintf(ha_cover.name, sizeof(ha_cover.name), "%s Blind", settings.device_name);
    ha_switch_mount.device_name = settings.device_name;
    snprintf(ha_switch_mount.name, sizeof(ha_switch_mount.name), "%s Mount Right", settings.device_name);
    ha_switch_setup_enable.device_name = settings.device_name;
    snprintf(ha_switch_setup_enable.name, sizeof(ha_switch_setup_enable.name), "%s Setup Active", settings.device_name);
    ha_button_setup.device_name = settings.device_name;
    snprintf(ha_button_setup.name, sizeof(ha_button_setup.name), "%s Setup Button", settings.device_name);
    ha_rpm_max.device_name = settings.device_name;
    snprintf(ha_rpm_max.name, sizeof(ha_rpm_max.name), "%s RPM Max", settings.device_name);

    // create HA components
    subscribe_buffer_t *cover_handle = ha_lib_cover_register(&ha_cover);
    subscribe_buffer_t *switch_handle_mount = ha_lib_switch_register(&ha_switch_mount);
    subscribe_buffer_t *switch_handle = ha_lib_switch_register(&ha_switch_setup_enable);
    subscribe_buffer_t *button_handle = ha_lib_button_register(&ha_button_setup);
    //subscribe_buffer_t *text_handle = ha_lib_text_register(&ha_text_status);
    subscribe_buffer_t *number_handle = ha_lib_number_register(&ha_rpm_max);

    ha_lib_init(settings.mqtt_uri, settings.mqtt_user, settings.mqtt_pass);

    vTaskDelay(100);

#warning check mqtt connection before sending states

    // setup switch is off
    ha_lib_switch_update(switch_handle, "OFF");

    // set position of the roller
    float calc_pos = ((float)stepper.step_position / (float)settings.roller_limit * (float)100) + 1;
    ha_lib_cover_set_position(cover_handle, (int)calc_pos);

    // set dir invert switch
    if (settings.dir_invert)
    {
        stepper_set_invert(&stepper, 1);
        ha_lib_switch_update(switch_handle_mount, "ON");
    }
    else
    {
        ha_lib_switch_update(switch_handle_mount, "OFF");
    }

    // update max speed
    ha_lib_number_update(number_handle, settings.max_speed);

    // hold current to 0
    uint32_t data = 0;
    vTaskDelay(10);
    ret = stepper_read_reg(&stepper, 0x10, &data);
    ESP_LOGI("SYS", "STP %d %08x", ret, (unsigned int)data);
    data &= ~(0x0000001F);
    vTaskDelay(10);
    ret = stepper_write_reg(&stepper, 0x10, data);
    ESP_LOGI("SYS", "STP %d %08x", ret, (unsigned int)data);

    // short coils on standstill
    vTaskDelay(10);
    ret = stepper_read_reg(&stepper, 0x70, &data);
    ESP_LOGI("SYS", "STP %d %08x", ret, (unsigned int)data);
    data &= ~(0x00300000);
    data |= 0x00200000;
    vTaskDelay(10);
    ret = stepper_write_reg(&stepper, 0x70, data);
    ESP_LOGI("SYS", "STP %d %08x", ret, (unsigned int)data);

    uint8_t setup_active_state = 0;
    int32_t setup_limit_step = settings.roller_limit;
    uint8_t stepper_moving = 0;

    while (1)
    {
        vTaskDelay(1);

        if (set_cover_state)
        {
            ESP_LOGI("MAIN", "set_cover_state changed to %d\n", set_cover_state);

            // setup not active?
            if (!setup_active_state && stepper_ready(&stepper))
            {
                if (set_cover_state == 1)
                {
                    //ha_lib_cover_set_position(cover_handle, 100);
                    ha_lib_cover_set_state(cover_handle, "opening");
                    stepper_go_to_pos(&stepper, settings.max_speed, 0);
                    stepper_moving = 1;
                }
                else if (set_cover_state == 2)
                {
                    //ha_lib_cover_set_position(cover_handle, 0);
                    ha_lib_cover_set_state(cover_handle, "closing");
                    stepper_go_to_pos(&stepper, settings.max_speed, setup_limit_step);
                    stepper_moving = 2;
                }
                else if (set_cover_state == 3)
                {
                    float calc_pos = ((float)stepper.step_position / (float)setup_limit_step * (float)100) + 1;
                    ha_lib_cover_set_position(cover_handle, (int)calc_pos);
                    ha_lib_cover_set_state(cover_handle, "stopped");
                    stepper_stop(&stepper);
                    stepper_moving = 0;
                }
                else if (set_cover_state >= 100)
                {
                    int calc_pos = setup_limit_step * (set_cover_state - 100) / 100;
                    stepper_go_to_pos(&stepper, settings.max_speed, calc_pos);
                    stepper_moving = 3;
                }
            }

            set_cover_state = 0;
        }

        if (stepper_moving && stepper_ready(&stepper))
        {
            if (stepper_moving == 1)
            {
                ha_lib_cover_set_position(cover_handle, 0);
                ha_lib_cover_set_state(cover_handle, "open");
            }
            else if (stepper_moving == 2)
            {
                ha_lib_cover_set_position(cover_handle, 100);
                ha_lib_cover_set_state(cover_handle, "close");
            }
            else if (stepper_moving == 3)
            {
                float calc_pos = ((float)stepper.step_position / (float)setup_limit_step * (float)100) + 1;
                ha_lib_cover_set_position(cover_handle, (int)calc_pos);
                if (calc_pos == 100) {
                    ha_lib_cover_set_state(cover_handle, "close");
                }
                else {
                    ha_lib_cover_set_state(cover_handle, "open");
                }
            }

            settings.roller_pos = stepper.step_position;
            save_settings(&settings);

            stepper_moving = 0;
        }

        if (set_switch_mount)
        {
            ESP_LOGI("MAIN", "set_switch_mount changed to %d\n", set_switch_mount);
            if (set_switch_mount == 1)
            {
                stepper_set_invert(&stepper, 0);
                settings.dir_invert = 0;
                save_settings(&settings);
                ha_lib_switch_update(switch_handle_mount, "OFF");
            }
            else if (set_switch_mount == 2)
            {
                stepper_set_invert(&stepper, 1);
                settings.dir_invert = 1;
                save_settings(&settings);
                ha_lib_switch_update(switch_handle_mount, "ON");
            }
            set_switch_mount = 0;
        }

        if (set_switch_setup)
        {
            ESP_LOGI("MAIN", "set_switch_setup changed to %d\n", set_switch_setup);
            if (set_switch_setup == 1)
            {
                stepper_stop(&stepper);
                setup_active_state = 0;
                ha_lib_switch_update(switch_handle, "OFF");
            }
            else if (set_switch_setup == 2)
            {
                setup_active_state = 1;
                ha_lib_switch_update(switch_handle, "ON");
            }
            set_switch_setup = 0;
        }

        if (set_button_setup)
        {
            ESP_LOGI("MAIN", "set_button_setup changed to %d\n", set_button_setup);
            if (set_button_setup == 1)
            {
                // setup active?
                switch (setup_active_state)
                {
                case 1:
                    stepper_go_to_pos(&stepper, settings.max_speed, 16777215);
                    setup_active_state++;
                    break;
                case 2:
                    stepper_stop(&stepper);
                    setup_active_state++;
                    break;
                case 3:
                    setup_limit_step = stepper.step_position;
                    stepper_go_to_pos(&stepper, settings.max_speed, -16777215);
                    setup_active_state++;
                    break;
                case 4:
                    stepper_stop(&stepper);
                    vTaskDelay(200);
                    setup_limit_step -= stepper.step_position;

                    ESP_LOGI("STP", "Step limit %d", (int)setup_limit_step);

                    stepper_set_position(&stepper, 0);

                    ha_lib_switch_update(switch_handle, "OFF");
                    setup_active_state = 0;

                    settings.roller_limit = setup_limit_step;
                    settings.roller_pos = 0;
                    save_settings(&settings);

                    break;
                }
            }
            set_button_setup = 0;
        }
    }
}