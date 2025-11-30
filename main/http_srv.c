#include "http_srv.h"
#include "esp_ota_ops.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sys_cfg.h"
#include "cJSON.h"
#include "esp_log.h"
#include "secret.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define DEBUG_BUF_SIZE      2048

static vprintf_like_t debug_root_func;
static char debug_buf[DEBUG_BUF_SIZE] = {0};

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
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "404 error");
    return ESP_FAIL; // For any other URI send 404 and close socket
}

//-----------------------------------------------------------------------------
const char html[] = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"<title>Device Settings</title>\n"
"<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\" "
"integrity=\"sha384-ggOyR0iXCbMQv3Xipma34MD+dH/1fQ784/j6cY/iJTQUOhcWr7x9JvoRxT2MZw1T\" crossorigin=\"anonymous\">\n"
"<script>\n"
"async function loadSettings() {\n"
"  document.getElementById('rebootBtn').addEventListener('click', () => {\n"
"    if (confirm('Are you sure you want to reboot the device?')) {\n"
"      fetch('/api/reboot', { method: 'POST' })\n"
"        .then(response => {\n"
"          if (response.ok) {\n"
"            alert('Reboot initiated!');\n"
"          } else {\n"
"            alert('Failed to reboot.');\n"
"          }\n"
"        })\n"
"        .catch(() => alert('Error sending reboot request.'));\n"
"    }\n"
"  });\n"
"  const response = await fetch('/api/settings');\n"
"  if (response.ok) {\n"
"    const data = await response.json();\n"
"    document.getElementById('ip_address').value = data.ip_address;\n"
"    document.getElementById('gateway').value = data.gateway;\n"
"    document.getElementById('netmask').value = data.netmask;\n"
"    document.getElementById('dhcp_enable').checked = data.dhcp_enable;\n"
"    document.getElementById('dir_invert').checked = data.dir_invert;\n"
"    document.getElementById('max_speed').value = data.max_speed;\n"
"    document.getElementById('mqtt_uri').value = data.mqtt_uri;\n"
"    document.getElementById('mqtt_user').value = data.mqtt_user;\n"
"    document.getElementById('mqtt_pass').value = data.mqtt_pass;\n"
"    document.getElementById('device_name').value = data.device_name;\n"
"  }\n"
"  setInterval(debug_poll, 1000);\n"
"}\n"
"\n"
"async function saveSettings() {\n"
"  const data = {\n"
"    ip_address: document.getElementById('ip_address').value,\n"
"    gateway: document.getElementById('gateway').value,\n"
"    netmask: document.getElementById('netmask').value,\n"
"    dhcp_enable: document.getElementById('dhcp_enable').checked,\n"
"    dir_invert: document.getElementById('dir_invert').checked,\n"
"    max_speed: parseInt(document.getElementById('max_speed').value),\n"
"    mqtt_uri: document.getElementById('mqtt_uri').value,\n"
"    mqtt_user: document.getElementById('mqtt_user').value,\n"
"    mqtt_pass: document.getElementById('mqtt_pass').value,\n"
"    device_name: document.getElementById('device_name').value\n"
"  };\n"
"  const response = await fetch('/api/settings', {\n"
"    method: 'POST',\n"
"    headers: { 'Content-Type': 'application/json' },\n"
"    body: JSON.stringify(data)\n"
"  });\n"
"  if (response.ok) {\n"
"    alert('Settings saved!');\n"
"  } else {\n"
"    alert('Failed to save');\n"
"  }\n"
"}\n"
"\n"
"function upload_file() {\n"
"  document.getElementById(\"status_div\").innerHTML = \"Upload in progress\";\n"
"  let data = document.getElementById(\"file_sel\").files[0];\n"
"  const xhr = new XMLHttpRequest();\n"
"  xhr.open(\"POST\", \"/ota\", true);\n"
"  xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');\n"
"  xhr.upload.addEventListener(\"progress\", function (event) {\n"
"    if (event.lengthComputable) {\n"
"      document.getElementById(\"progress\").style.width = (event.loaded / event.total) * 100 + \"%\";\n"
"    }\n"
"  });\n"
"  xhr.onreadystatechange = function () {\n"
"    if (xhr.readyState === XMLHttpRequest.DONE) {\n"
"      const status = xhr.status;\n"
"      if (status >= 200 && status < 400) {\n"
"        document.getElementById(\"status_div\").innerHTML = \"Upload accepted. Device will reboot.\";\n"
"      } else {\n"
"        document.getElementById(\"status_div\").innerHTML = \"Upload rejected!\";\n"
"      }\n"
"    }\n"
"  };\n"
"  xhr.send(data);\n"
"  return false;\n"
"}\n"
"\n"
"function debug_poll() {\n"
"  var xhttp = new XMLHttpRequest();\n"
"  xhttp.onreadystatechange = function () {\n"
"    if (xhttp.readyState == XMLHttpRequest.DONE) {\n"
"      if (xhttp.status == 200 && xhttp.responseText.length > 0) {\n"
"        let str = xhttp.responseText.replace(/(?:\\r\\n|\\r|\\n)/g, '<br>');\n"
"        let obj = document.getElementById(\"debug-terminal\");\n"
"        obj.innerHTML += str;\n"
"        obj.scrollTop = obj.scrollHeight;\n"
"      }\n"
"    }\n"
"  };\n"
"  xhttp.open(\"GET\", \"log\", true);\n"
"  xhttp.send();\n"
"}\n"
"\n"
"window.onload = loadSettings;\n"
"</script>\n"
"</head>\n"
"<body class=\"bg-dark\" style=\"padding-bottom:100px;\">\n"
"<div class=\"well\">\n"
"<div class=\"container bg-light px-5 py-3\" style=\"max-width:1200px;\">\n"
"<div class=\"row\">\n"
"<div class=\"col\" style=\"text-align: center;\">\n"
"<h1>Device Configuration " SW_VERSION_STR "</h1>\n"
"<form onsubmit=\"event.preventDefault(); saveSettings();\">\n"
"<label>Device Name: <input type=\"text\" id=\"device_name\"></label><br>\n"
"<label>Direction Invert: <input type=\"checkbox\" id=\"dir_invert\"></label><br>\n"
"<label>Max Speed: <input type=\"number\" id=\"max_speed\"></label><br>\n"
"<h3>Network Configuration</h3>\n"
"<label>IP Address: <input type=\"text\" id=\"ip_address\" required pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\"></label><br>\n"
"<label>Gateway: <input type=\"text\" id=\"gateway\" required pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\"></label><br>\n"
"<label>Netmask: <input type=\"text\" id=\"netmask\" required pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\"></label><br>\n"
"<label>DHCP Enable: <input type=\"checkbox\" id=\"dhcp_enable\"></label><br>\n"
"<h3>MQTT Configuration</h3>\n"
"<label>MQTT URI: <input type=\"text\" id=\"mqtt_uri\"></label><br>\n"
"<label>MQTT User: <input type=\"text\" id=\"mqtt_user\"></label><br>\n"
"<label>MQTT Pass: <input type=\"password\" id=\"mqtt_pass\"></label><br>\n"
"<button type=\"submit\">Save Settings</button>\n"
"</form>\n"
"<hr />\n"
"<h3>Firmware Update</h3>\n"
"<button onclick=\"file_sel.click();\">Select Binary</button><br>\n"
"<div class=\"progress\" style=\"height: 20px;\">\n"
"<div class=\"progress-bar\" id=\"progress\"></div>\n"
"</div>\n"
"<div class=\"status\" id=\"status_div\"></div><br>\n"
"<input type=\"file\" id=\"file_sel\" onchange=\"upload_file()\" style=\"display: none;\"></input><br>\n"
"<hr />\n"
"<button id=\"rebootBtn\">Reboot Device</button>\n"
"</div>\n"
"<div class=\"col\">\n"
"<p class=\"border p-2 m-2 bg-secondary text-white overflow-auto\" style=\"max-height:800px;\" id=\"debug-terminal\"></p>\n"
"</div>\n"
"</div>\n"
"</div>\n"
"</body>\n"
"</html>\n";

static esp_err_t index_get_handler(httpd_req_t *req)
{
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

static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    // Respond immediately
    httpd_resp_set_status(req, "200 OK");
    const char *resp = "{\"status\":\"rebooting\"}";
    httpd_resp_send(req, resp, strlen(resp));

    // Wait briefly to ensure response is sent
    vTaskDelay(pdMS_TO_TICKS(100));

    // Restart the ESP device
    esp_restart();

    return ESP_OK;
}

static esp_err_t debug_logs_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, debug_buf, strlen(debug_buf));
    memset(debug_buf, 0, sizeof(debug_buf));
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

        static httpd_uri_t reboot_uri = {
            .uri = "/api/reboot",
            .method = HTTP_POST,
            .handler = api_reboot_handler,
        };
        httpd_register_uri_handler(server, &reboot_uri);

        static const httpd_uri_t ota_post =
            {
                .uri = "/ota",
                .method = HTTP_POST,
                .handler = ota_post_handler,
            };
        httpd_register_uri_handler(server, &ota_post);

        static const httpd_uri_t debug_logs_uri = {
            .uri = "/log",
            .method = HTTP_GET,
            .handler = debug_logs_get_handler,
        };
        httpd_register_uri_handler(server, &debug_logs_uri);
    }

    return NULL;
}

void str_cat_move(char *buffer, int buf_size, char *buffer_append)
{
    uint16_t size = strlen(buffer_append);

    // use buffer size minus 1
    buf_size--;

    // buffer already filled to the top?
    if (strlen(buffer) >= buf_size)
    {
        // copy the entire log buffer back the amount of data that is to be appended
        memcpy(buffer, &buffer[size], buf_size - size);

        // copy new data into the buffer
        memcpy(&buffer[buf_size - size], buffer_append, size);

        // be sure to zero out the end
        buffer[buf_size] = 0;
    }
    // when appending the new string, buffer will overflow?
    else if (strlen(buffer) + strlen(buffer_append) > buf_size)
    {
        // how many bytes will the buffer overflow?
        uint16_t overflow = (strlen(buffer) + size) % buf_size;

        // copy the entire log buffer back the amount of data that is overflown
        memcpy(buffer, &buffer[overflow], buf_size - overflow);

        // copy new data into the buffer
        memcpy(&buffer[buf_size - size], buffer_append, size);

        // be sure to zero out the end
        buffer[buf_size] = 0;
    }
    // new string fits into buffer without problems
    else
    {
        // copy new data into the buffer
        memcpy(&buffer[strlen(buffer)], buffer_append, size);
    }
}

static int httpDebugPrintf(const char *fmt, va_list lst)
{
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), fmt, lst);
    str_cat_move(debug_buf, sizeof(debug_buf) - 1, buffer);
    return debug_root_func(fmt, lst);
}

// Initialization function to override log function
void setup_log_capture(void)
{
    debug_root_func = esp_log_set_vprintf(httpDebugPrintf);
}