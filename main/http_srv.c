#include "http_srv.h"
#include "esp_ota_ops.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sys_cfg.h"
#include "cJSON.h"
#include "esp_log.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

//-----------------------------------------------------------------------------
uint32_t system_uptime_ms(void)
{
    // esp_timer_get_time returns time in uSec
    return xTaskGetTickCount();
}

//-----------------------------------------------------------------------------
char auth_buffer[512];
const char ota_html_file[] = "\
<style>\n\
.progress {margin: 15px auto;  max-width: 500px;height: 30px;}\n\
.progress .progress__bar {\n\
  height: 100%; width: 1%; border-radius: 15px;\n\
  background: repeating-linear-gradient(135deg,#336ffc,#036ffc 15px,#1163cf 15px,#1163cf 30px); }\n\
 .status {font-weight: bold; font-size: 30px;};\n\
</style>\n\
<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/twitter-bootstrap/2.2.1/css/bootstrap.min.css\">\n\
<div class=\"well\" style=\"text-align: center;\">\n\
  <div class=\"btn\" onclick=\"file_sel.click();\"><i class=\"icon-upload\" style=\"padding-right: 5px;\"></i>Upload Firmware</div>\n\
  <div class=\"progress\"><div class=\"progress__bar\" id=\"progress\"></div></div>\n\
  <div class=\"status\" id=\"status_div\"></div>\n\
</div>\n\
<input type=\"file\" id=\"file_sel\" onchange=\"upload_file()\" style=\"display: none;\">\n\
<script>\n\
function upload_file() {\n\
  document.getElementById(\"status_div\").innerHTML = \"Upload in progress\";\n\
  let data = document.getElementById(\"file_sel\").files[0];\n\
  xhr = new XMLHttpRequest();\n\
  xhr.open(\"POST\", \"/ota\", true);\n\
  xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');\n\
  xhr.upload.addEventListener(\"progress\", function (event) {\n\
     if (event.lengthComputable) {\n\
    	 document.getElementById(\"progress\").style.width = (event.loaded / event.total) * 100 + \"%\";\n\
     }\n\
  });\n\
  xhr.onreadystatechange = function () {\n\
    if(xhr.readyState === XMLHttpRequest.DONE) {\n\
      var status = xhr.status;\n\
      if (status >= 200 && status < 400)\n\
      {\n\
        document.getElementById(\"status_div\").innerHTML = \"Upload accepted. Device will reboot.\";\n\
      } else {\n\
        document.getElementById(\"status_div\").innerHTML = \"Upload rejected!\";\n\
      }\n\
    }\n\
  };\n\
  xhr.send(data);\n\
  return false;\n\
}\n\
</script>";

//-----------------------------------------------------------------------------
static esp_err_t ota_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_send(req, ota_html_file, strlen(ota_html_file));
    return ESP_OK;
}

//-----------------------------------------------------------------------------
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    char buf[256];
    httpd_resp_set_status(req, HTTPD_500); // Assume failure

    int ret, remaining = req->content_len;
    printf("Receiving\n");

    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (update_partition == NULL)
    {
        printf("Uh oh, bad things\n");
        goto return_failure;
    }

    printf("Writing partition: type %d, subtype %d, offset 0x%08x\n", update_partition->type, update_partition->subtype, (unsigned int)update_partition->address);
    printf("Running partition: type %d, subtype %d, offset 0x%08x\n", running->type, running->subtype, (unsigned int)running->address);
    esp_err_t err = ESP_OK;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK)
    {
        printf("esp_ota_begin failed (%s)", esp_err_to_name(err));
        goto return_failure;
    }
    while (remaining > 0)
    {
        // Read the data for the request
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                // Retry receiving if timeout occurred
                continue;
            }

            goto return_failure;
        }

        size_t bytes_read = ret;

        remaining -= bytes_read;
        err = esp_ota_write(update_handle, buf, bytes_read);
        if (err != ESP_OK)
        {
            goto return_failure;
        }
    }

    printf("Receiving done\n");

    // End response
    if ((esp_ota_end(update_handle) == ESP_OK) &&
        (esp_ota_set_boot_partition(update_partition) == ESP_OK))
    {
        printf("OTA Success?!\n Rebooting\n");
        fflush(stdout);

        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_send(req, NULL, 0);

        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();

        return ESP_OK;
    }
    printf("OTA End failed (%s)!\n", esp_err_to_name(err));

return_failure:
    if (update_handle)
    {
        esp_ota_abort(update_handle);
    }

    httpd_resp_set_status(req, HTTPD_500); // Assume failure
    httpd_resp_send(req, NULL, 0);
    return ESP_FAIL;
}

//-----------------------------------------------------------------------------
const char reset_html_file[] = "\
<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/twitter-bootstrap/2.2.1/css/bootstrap.min.css\">\n\
<div class=\"well\" style=\"text-align: center;\">\n\
  <div class=\"btn\" onclick=\"reset_btn.click();\"><i class=\"icon-wrench\" style=\"padding-right: 5px;\"></i>Reset Device</div>\n\
  <div class=\"status\" id=\"status_div\"></div>\n\
</div>\n\
<input type=\"button\" id=\"reset_btn\" onclick=\"reset_device()\" style=\"display: none;\">\n\
<script>\n\
function reset_device() {\n\
  document.getElementById(\"status_div\").innerHTML = \"Resetting Device...\";\n\
  xhr = new XMLHttpRequest();\n\
  xhr.open(\"POST\", \"/reset\", true);\n\
  xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');\n\
  xhr.onreadystatechange = function () {\n\
    if(xhr.readyState === XMLHttpRequest.DONE) {\n\
      var status = xhr.status;\n\
      if (status >= 200 && status < 400)\n\
      {\n\
        document.getElementById(\"status_div\").innerHTML = \"Device is rebooting, reload this page.\";\n\
      } else {\n\
        document.getElementById(\"status_div\").innerHTML = \"Device did NOT reboot?!\";\n\
      }\n\
    }\n\
  };\n\
  xhr.send("
                               ");\n\
  return false;\n\
}\n\
</script>";

//-----------------------------------------------------------------------------
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    printf("Rebooting\n");
    fflush(stdout);

    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, NULL, 0);

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}

//-----------------------------------------------------------------------------
static esp_err_t reset_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_send(req, reset_html_file, strlen(reset_html_file));
    return ESP_OK;
}

//-----------------------------------------------------------------------------
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "404 error");
    return ESP_FAIL; // For any other URI send 404 and close socket
}

//-----------------------------------------------------------------------------
static esp_err_t index_get_handler(httpd_req_t *req)
{
    const char *html = 
        "<!DOCTYPE html>"
        "<html><head><title>Device Settings</title>"
        "<script>"
        "async function loadSettings() {"
        "const response = await fetch('/api/settings');"
        "if (response.ok) {"
        "const data = await response.json();"
        "document.getElementById('ip_address').value = data.ip_address;"
        "document.getElementById('gateway').value = data.gateway;"
        "document.getElementById('netmask').value = data.netmask;"
        "document.getElementById('dhcp_enable').checked = data.dhcp_enable;"
        "document.getElementById('dir_invert').checked = data.dir_invert;"
        "document.getElementById('max_speed').value = data.max_speed;"
        "document.getElementById('mqtt_uri').value = data.mqtt_uri;"
        "document.getElementById('mqtt_user').value = data.mqtt_user;"
        "document.getElementById('mqtt_pass').value = data.mqtt_pass;"
        "document.getElementById('device_name').value = data.device_name;"
        "}"
        "}"
        "async function saveSettings() {"
        "const data = {"
        "ip_address: document.getElementById('ip_address').value,"
        "gateway: document.getElementById('gateway').value,"
        "netmask: document.getElementById('netmask').value,"
        "dhcp_enable: document.getElementById('dhcp_enable').checked,"
        "dir_invert: document.getElementById('dir_invert').checked,"
        "max_speed: parseInt(document.getElementById('max_speed').value),"
        "mqtt_uri: document.getElementById('mqtt_uri').value,"
        "mqtt_user: document.getElementById('mqtt_user').value,"
        "mqtt_pass: document.getElementById('mqtt_pass').value,"
        "device_name: document.getElementById('device_name').value"
        "};"
        "const response = await fetch('/api/settings', {"
        "method: 'POST',"
        "headers: { 'Content-Type': 'application/json' },"
        "body: JSON.stringify(data)"
        "});"
        "if (response.ok) { alert('Settings saved!'); } else { alert('Failed to save'); } }"
        "window.onload = loadSettings;"
        "</script>"
        "</head><body>"
        "<h1>Device Configuration</h1>"
        "<form onsubmit='event.preventDefault(); saveSettings();'>"
        "<label>IP Address: <input type='text' id='ip_address'></label><br>"
        "<label>Gateway: <input type='text' id='gateway'></label><br>"
        "<label>Netmask: <input type='text' id='netmask'></label><br>"
        "<label>DHCP Enable: <input type='checkbox' id='dhcp_enable'></label><br>"
        "<label>Direction Invert: <input type='checkbox' id='dir_invert'></label><br>"
        "<label>Max Speed: <input type='number' id='max_speed'></label><br>"
        "<h2>MQTT Settings</h2>"
        "<label>MQTT URI: <input type='text' id='mqtt_uri'></label><br>"
        "<label>MQTT User: <input type='text' id='mqtt_user'></label><br>"
        "<label>MQTT Pass: <input type='password' id='mqtt_pass'></label><br>"
        "<label>Device Name: <input type='text' id='device_name'></label><br>"
        "<button type='submit'>Save Settings</button>"
        "</form></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t api_get_settings_handler(httpd_req_t *req)
{
    // Convert settings to JSON
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ip_address", settings.ip_address);
    cJSON_AddStringToObject(json, "gateway", settings.gateway);
    cJSON_AddStringToObject(json, "netmask", settings.netmask);
    cJSON_AddBoolToObject(json, "dhcp_enable", settings.dhcp_enable);
    cJSON_AddBoolToObject(json, "dir_invert", settings.dir_invert);
    cJSON_AddNumberToObject(json, "max_speed", settings.max_speed);
    cJSON_AddStringToObject(json, "mqtt_uri", settings.mqtt_uri);
    cJSON_AddStringToObject(json, "mqtt_user", settings.mqtt_user);
    cJSON_AddStringToObject(json, "mqtt_pass", settings.mqtt_pass);
    cJSON_AddStringToObject(json, "device_name", settings.device_name);

    char *json_str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    cJSON_Delete(json);
    free(json_str);
    return ESP_OK;
}

static esp_err_t api_post_settings_handler(httpd_req_t *req)
{
    int data_read = req->content_len;
    if (data_read <= 0 || data_read > 4096)
    {
        // fallback or reject
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *buf = malloc(data_read + 1);
    if (!buf)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, data_read);
    if (ret <= 0)
    {
        free(buf);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI("HTT", "%d %d %s", ret, data_read, buf);

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Update settings
    cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(json, "ip_address");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(settings.ip_address, item->valuestring, sizeof(settings.ip_address));

    item = cJSON_GetObjectItemCaseSensitive(json, "gateway");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(settings.gateway, item->valuestring, sizeof(settings.gateway));

    item = cJSON_GetObjectItemCaseSensitive(json, "netmask");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(settings.netmask, item->valuestring, sizeof(settings.netmask));

    item = cJSON_GetObjectItemCaseSensitive(json, "dhcp_enable");
    if (cJSON_IsBool(item))
        settings.dhcp_enable = cJSON_IsTrue(item);

    item = cJSON_GetObjectItemCaseSensitive(json, "dir_invert");
    if (cJSON_IsBool(item))
        settings.dir_invert = cJSON_IsTrue(item);

    item = cJSON_GetObjectItemCaseSensitive(json, "max_speed");
    if (cJSON_IsNumber(item))
        settings.max_speed = item->valueint;

    item = cJSON_GetObjectItemCaseSensitive(json, "mqtt_uri");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(settings.mqtt_uri, item->valuestring, sizeof(settings.mqtt_uri));

    item = cJSON_GetObjectItemCaseSensitive(json, "mqtt_user");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(settings.mqtt_user, item->valuestring, sizeof(settings.mqtt_user));

    item = cJSON_GetObjectItemCaseSensitive(json, "mqtt_pass");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(settings.mqtt_pass, item->valuestring, sizeof(settings.mqtt_pass));

    item = cJSON_GetObjectItemCaseSensitive(json, "device_name");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(settings.device_name, item->valuestring, sizeof(settings.device_name));

    cJSON_Delete(json);

    print_settings(&settings);

    save_settings(&settings);

    // Return success
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, "{\"status\":\"ok\"}", strlen("{\"status\":\"ok\"}"));
    return ESP_OK;
}

//-----------------------------------------------------------------------------
httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    printf("Starting server on port %d\n", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK)
    {
        static httpd_uri_t index_get =
            {
                .uri = "/",
                .method = HTTP_GET,
                .handler = index_get_handler,
            };
        httpd_register_uri_handler(server, &index_get);

        static httpd_uri_t api_get_uri = {
            .uri = "/api/settings",
            .method = HTTP_GET,
            .handler = api_get_settings_handler,
        };
        httpd_register_uri_handler(server, &api_get_uri);
        
        static httpd_uri_t api_post_uri = {
            .uri = "/api/settings",
            .method = HTTP_POST,
            .handler = api_post_settings_handler,
        };
        httpd_register_uri_handler(server, &api_post_uri);

        static const httpd_uri_t ota_post =
            {
                .uri = "/ota",
                .method = HTTP_POST,
                .handler = ota_post_handler,
            };
        httpd_register_uri_handler(server, &ota_post);

        static httpd_uri_t ota_get =
            {
                .uri = "/ota",
                .method = HTTP_GET,
                .handler = ota_get_handler,
            };
        httpd_register_uri_handler(server, &ota_get);

        static httpd_uri_t reset_post =
            {
                .uri = "/reset",
                .method = HTTP_POST,
                .handler = reset_post_handler,
            };
        httpd_register_uri_handler(server, &reset_post);

        static httpd_uri_t reset_get =
            {
                .uri = "/reset",
                .method = HTTP_GET,
                .handler = reset_get_handler,
            };
        httpd_register_uri_handler(server, &reset_get);
    }

    return NULL;
}