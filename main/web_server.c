#include "web_server.h"
#include "modbus_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "simple_wifi_sta.h"
#include "mqtt.h"

// 日志标签
static const char *TAG = "web_server";

esp_err_t get_html_handler(httpd_req_t *req)
{
    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);

    return ESP_OK;
}

// Modbus配置获取处理函数
esp_err_t get_modbus_config_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "poll_interval", modbus_config.poll_interval);
    cJSON_AddNumberToObject(root, "group_count", modbus_config.group_count);

    cJSON *groups = cJSON_CreateArray();
    for (int i = 0; i < modbus_config.group_count; i++)
    {
        cJSON *group = cJSON_CreateObject();
        cJSON_AddBoolToObject(group, "enabled", modbus_config.groups[i].enabled);
        cJSON_AddNumberToObject(group, "slave_addr", modbus_config.groups[i].slave_addr);
        cJSON_AddNumberToObject(group, "function_code", modbus_config.groups[i].function_code);
        cJSON_AddNumberToObject(group, "start_addr", modbus_config.groups[i].start_addr);
        cJSON_AddNumberToObject(group, "reg_count", modbus_config.groups[i].reg_count);
        cJSON_AddNumberToObject(group, "uart_port", modbus_config.groups[i].uart_port);
        cJSON_AddItemToArray(groups, group);
    }

    cJSON_AddItemToObject(root, "groups", groups);

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Modbus配置更新处理函数
esp_err_t update_modbus_config_handler(httpd_req_t *req)
{
    char *content = malloc(1024);
    int ret = httpd_req_recv(req, content, 1024);
    if (ret <= 0)
    {
        free(content);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    free(content);

    if (root == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *poll_interval = cJSON_GetObjectItem(root, "poll_interval");
    cJSON *group_count = cJSON_GetObjectItem(root, "group_count");
    cJSON *groups = cJSON_GetObjectItem(root, "groups");

    if (poll_interval)
        modbus_config.poll_interval = poll_interval->valueint;
    if (group_count)
    {
        modbus_config.group_count = group_count->valueint;
        if (modbus_config.group_count > MAX_POLL_GROUPS)
        {
            modbus_config.group_count = MAX_POLL_GROUPS;
        }
    }

    if (groups && cJSON_IsArray(groups))
    {
        int array_size = cJSON_GetArraySize(groups);
        for (int i = 0; i < array_size && i < modbus_config.group_count; i++)
        {
            cJSON *group = cJSON_GetArrayItem(groups, i);
            cJSON *enabled = cJSON_GetObjectItem(group, "enabled");
            cJSON *slave_addr = cJSON_GetObjectItem(group, "slave_addr");
            cJSON *function_code = cJSON_GetObjectItem(group, "function_code");
            cJSON *start_addr = cJSON_GetObjectItem(group, "start_addr");
            cJSON *reg_count = cJSON_GetObjectItem(group, "reg_count");
            cJSON *uart_port = cJSON_GetObjectItem(group, "uart_port");

            if (enabled)
                modbus_config.groups[i].enabled = enabled->valueint;
            if (slave_addr)
            {
                int addr = slave_addr->valueint;
                if (addr >= 1 && addr <= 247)
                {
                    modbus_config.groups[i].slave_addr = addr;
                }
            }
            if (function_code)
            {
                int fc = 0;
                if (cJSON_IsNumber(function_code))
                {
                    fc = function_code->valueint;
                }
                else if (cJSON_IsString(function_code))
                {
                    fc = atoi(function_code->valuestring);
                }

                if (fc >= 1 && fc <= 4)
                {
                    modbus_config.groups[i].function_code = fc;
                }
            }
            if (start_addr)
                modbus_config.groups[i].start_addr = start_addr->valueint;
            if (reg_count)
            {
                modbus_config.groups[i].reg_count = reg_count->valueint;
                if (modbus_config.groups[i].reg_count > MAX_REGS)
                {
                    modbus_config.groups[i].reg_count = MAX_REGS;
                }
            }
            if (uart_port)
            {
                int port = uart_port->valueint;
                if (port == 1 || port == 2)
                {
                    modbus_config.groups[i].uart_port = port;
                }
            }
        }
    }

    cJSON_Delete(root);
    // 在发送响应之前保存配置
    esp_err_t save_err = save_modbus_config_to_nvs();
    if (save_err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS存储失败");
    }

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// MQTT配置获取处理函数
esp_err_t get_mqtt_config_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    mqtt_config_t current_config;
    mqtt_get_config(&current_config);

    cJSON_AddBoolToObject(root, "enabled", current_config.enabled);
    cJSON_AddStringToObject(root, "broker_url", current_config.broker_url);
    cJSON_AddStringToObject(root, "username", current_config.username);
    cJSON_AddStringToObject(root, "topic", current_config.topic);

    // 添加组ID数组
    cJSON *groups = cJSON_CreateArray();
    cJSON *parse_methods = cJSON_CreateArray();
    for (int i = 0; i < current_config.group_count; i++) {
        cJSON_AddItemToArray(groups, cJSON_CreateNumber(current_config.group_ids[i]));
        cJSON_AddItemToArray(parse_methods, cJSON_CreateNumber(current_config.parse_methods[i]));
    }
    cJSON_AddItemToObject(root, "group_ids", groups);
    cJSON_AddItemToObject(root, "parse_methods", parse_methods);

    cJSON_AddNumberToObject(root, "publish_interval", current_config.publish_interval);
    cJSON_AddBoolToObject(root, "connected", mqtt_is_connected());

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// MQTT配置更新处理函数
esp_err_t update_mqtt_config_handler(httpd_req_t *req)
{
    char *content = malloc(1024);
    int ret = httpd_req_recv(req, content, 1024);
    if (ret <= 0) {
        free(content);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    free(content);

    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    mqtt_config_t new_config;
    mqtt_get_config(&new_config); // 获取当前配置作为基础

    // 解析新的配置
    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    cJSON *broker_url = cJSON_GetObjectItem(root, "broker_url");
    cJSON *username = cJSON_GetObjectItem(root, "username");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    cJSON *topic = cJSON_GetObjectItem(root, "topic");
    cJSON *group_ids = cJSON_GetObjectItem(root, "group_ids");
    cJSON *parse_methods = cJSON_GetObjectItem(root, "parse_methods");
    cJSON *publish_interval = cJSON_GetObjectItem(root, "publish_interval");

    if (enabled) new_config.enabled = enabled->valueint;
    if (broker_url && cJSON_IsString(broker_url)) {
        strncpy(new_config.broker_url, broker_url->valuestring, sizeof(new_config.broker_url) - 1);
    }
    if (username && cJSON_IsString(username)) {
        strncpy(new_config.username, username->valuestring, sizeof(new_config.username) - 1);
    }
    if (password && cJSON_IsString(password)) {
        strncpy(new_config.password, password->valuestring, sizeof(new_config.password) - 1);
    }
    if (topic && cJSON_IsString(topic)) {
        strncpy(new_config.topic, topic->valuestring, sizeof(new_config.topic) - 1);
    }

    // 处理组ID和解析方式数组
    if (group_ids && cJSON_IsArray(group_ids) && parse_methods && cJSON_IsArray(parse_methods)) {
        int count = cJSON_GetArraySize(group_ids);
        int parse_count = cJSON_GetArraySize(parse_methods);
        
        // 使用较小的数组大小作为实际数量
        count = (count < parse_count) ? count : parse_count;
        if (count > MAX_POLL_GROUPS) count = MAX_POLL_GROUPS;
        new_config.group_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *group_id = cJSON_GetArrayItem(group_ids, i);
            cJSON *parse_method = cJSON_GetArrayItem(parse_methods, i);
            
            if (group_id && cJSON_IsNumber(group_id)) {
                new_config.group_ids[i] = group_id->valueint;
            }
            if (parse_method && cJSON_IsNumber(parse_method)) {
                new_config.parse_methods[i] = parse_method->valueint;
            }
        }
    }

    if (publish_interval) new_config.publish_interval = publish_interval->valueint;

    // 更新MQTT配置
    esp_err_t err = mqtt_update_config(&new_config);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update MQTT configuration");
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    // 保存MQTT配置到NVS
    err = save_mqtt_config(&mqtt_config);
    if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS存储失败");
    }
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// WiFi配置获取处理函数
esp_err_t wifi_config_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0)
    {
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (root == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");

    if (!ssid || !password || !cJSON_IsString(ssid) || !cJSON_IsString(password))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid or password");
        return ESP_FAIL;
    }

    // 保存wifi配置，不立即应用
    esp_err_t err = wifi_save_config(ssid->valuestring, password->valuestring);
    if (err != ESP_OK)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
        return ESP_FAIL;
    }

    // 返回成功信息，提示用户需要重启设备
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "WiFi configuration saved ,please restart");

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(response);
    cJSON_Delete(root);

    return ESP_OK;
}

// URI处理结构
static const httpd_uri_t html = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_html_handler,
    .user_ctx = NULL};

static const httpd_uri_t mqtt_config_get = {
    .uri = "/api/mqtt/config",
    .method = HTTP_GET,
    .handler = get_mqtt_config_handler,
    .user_ctx = NULL};

static const httpd_uri_t mqtt_config_post = {
    .uri = "/api/mqtt/config",
    .method = HTTP_POST,
    .handler = update_mqtt_config_handler,
    .user_ctx = NULL};

static const httpd_uri_t modbus_config_get = {
    .uri = "/api/config",
    .method = HTTP_GET,
    .handler = get_modbus_config_handler,
    .user_ctx = NULL};

static const httpd_uri_t modbus_config_post = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = update_modbus_config_handler,
    .user_ctx = NULL};

static const httpd_uri_t wifi_config = {
    .uri = "/api/wifi/config",
    .method = HTTP_POST,
    .handler = wifi_config_handler,
    .user_ctx = NULL};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &html);
        httpd_register_uri_handler(server, &modbus_config_get);
        httpd_register_uri_handler(server, &modbus_config_post);
        httpd_register_uri_handler(server, &mqtt_config_get);
        httpd_register_uri_handler(server, &mqtt_config_post);
        httpd_register_uri_handler(server, &wifi_config);
        ESP_LOGI(TAG, "HTTP服务器启动成功");
        return server;
    }

    ESP_LOGE(TAG, "HTTP服务器启动失败");
    return NULL;
}