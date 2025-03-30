#include <string.h>
#include "mqtt.h"
#include "esp_log.h"
#include "modbus_config.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

// 日志标签
static const char *TAG = "mqtt";

// 静态JSON缓冲区，避免堆栈上分配内存
#define JSON_BUFFER_SIZE 8192
static char json_buffer[JSON_BUFFER_SIZE];

// MQTT配置结构体的默认配置
mqtt_config_t mqtt_config = {
    .broker_url = "",
    .username = "",
    .password = "",
    .topic = "modbus/data",  // 默认发布主题
    .enabled = false,        // 默认不启用
    .group_ids = {0},        // 默认只发布第一组
    .group_count = 1,        // 默认发布1个组
    .publish_interval = 5000,
    .parse_methods = {// 默认所有组使用有符号16位整数解析
                      [0 ... MAX_POLL_GROUPS - 1] = PARSE_INT16_UNSIGNED}};

// MQTT客户端句柄
static esp_mqtt_client_handle_t mqtt_client = NULL;
// MQTT连接状态标志
static bool mqtt_connected = false;
// 发布任务句柄
static TaskHandle_t publish_task_handle = NULL;

// MQTT事件处理函数
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected to broker");
        mqtt_connected = true;
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected from broker");
        mqtt_connected = false;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Error");
        break;

    default:
        break;
    }
}

// MQTT数据发布任务
static void mqtt_publish_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    ESP_LOGI(TAG, "MQTT publish task started");

    while (1) {
        if (mqtt_connected && mqtt_config.enabled) {
            ESP_LOGI(TAG, "Starting MQTT publish cycle");
            
            cJSON *root = cJSON_CreateObject();
            if (!root) {
                ESP_LOGE(TAG, "Failed to create root JSON object");
                goto delay_continue;
            }

            bool any_group_ready = false;

            for (int i = 0; i < mqtt_config.group_count; i++) {
                uint8_t group_id = mqtt_config.group_ids[i];
                if (group_id >= modbus_config.group_count ||
                    !modbus_data.register_ready[group_id]) {
                    ESP_LOGD(TAG, "Skipping group %d - not ready", group_id);
                    continue;
                }

                ESP_LOGI(TAG, "Processing group %d", group_id);
                
                // 为每个组创建一个数组
                cJSON *group_data = cJSON_CreateArray();
                if (!group_data) {
                    ESP_LOGE(TAG, "Failed to create group data array for group %d", group_id);
                    continue;
                }

                bool success = true;
                uint8_t function_code = modbus_config.groups[group_id].function_code;
                uint16_t count = modbus_config.groups[group_id].reg_count;

                //ESP_LOGI(TAG, "Processing function code %d with %d registers", function_code, count);

                switch(function_code) {
                    case 1:
                    case 2: {
                        //ESP_LOGI(TAG, "Processing coils/discrete inputs for group %d", group_id);
                        uint8_t *bits = (function_code == 1) ?
                                      modbus_data.coils[group_id] :
                                      modbus_data.discrete_inputs[group_id];

                        for (uint16_t j = 0; j < count && success; j++) {
                            uint8_t byte_index = j / 8;
                            uint8_t bit_index = j % 8;
                            bool bit_value = (bits[byte_index] >> bit_index) & 0x01;
                            cJSON *num = cJSON_CreateNumber(bit_value);
                            if (!num) {
                                ESP_LOGE(TAG, "Failed to create JSON number for bit %d", j);
                                success = false;
                            } else {
                                cJSON_AddItemToArray(group_data, num);
                            }
                        }
                        break;
                    }

                    case 3:
                    case 4: {
                        uint16_t *regs = (function_code == 3) ? 
                                        modbus_data.holding_regs[group_id] :
                                        modbus_data.input_regs[group_id];
                        
                        parse_method_t method = mqtt_config.parse_methods[group_id];
                        //ESP_LOGI(TAG, "Using parse method %d", method);
                        
                        switch(method) {
                            case PARSE_INT16_SIGNED:
                            case PARSE_INT16_UNSIGNED: {
                                for (uint16_t j = 0; j < count && success; j++) {
                                    cJSON *num;
                                    if (method == PARSE_INT16_SIGNED) {
                                        num = cJSON_CreateNumber((int16_t)regs[j]);
                                    } else {
                                        num = cJSON_CreateNumber(regs[j]);
                                    }
                                    if (!num) {
                                        ESP_LOGE(TAG, "Failed to create JSON number for register %d", j);
                                        success = false;
                                    } else {
                                        cJSON_AddItemToArray(group_data, num);
                                    }
                                }
                                break;
                            }

                            case PARSE_INT32_ABCD:
                            case PARSE_INT32_CDAB:
                            case PARSE_INT32_BADC:
                            case PARSE_INT32_DCBA: {
                                for (uint16_t j = 0; j < count && success; j += 2) {
                                    uint32_t value;
                                    switch(method) {
                                        case PARSE_INT32_ABCD:
                                            value = ((uint32_t)regs[j] << 16) | regs[j + 1];
                                            break;
                                        case PARSE_INT32_CDAB:
                                            value = ((uint32_t)regs[j + 1] << 16) | regs[j];
                                            break;
                                        case PARSE_INT32_BADC:
                                            value = ((uint32_t)(regs[j] & 0xFF00) << 8) |
                                                   ((uint32_t)(regs[j] & 0x00FF) << 24) |
                                                   ((uint32_t)(regs[j + 1] & 0xFF00) >> 8) |
                                                   ((uint32_t)(regs[j + 1] & 0x00FF) << 8);
                                            break;
                                        case PARSE_INT32_DCBA:
                                            value = ((uint32_t)(regs[j + 1] & 0xFF00) >> 8) |
                                                   ((uint32_t)(regs[j + 1] & 0x00FF) << 8) |
                                                   ((uint32_t)(regs[j] & 0xFF00) >> 24) |
                                                   ((uint32_t)(regs[j] & 0x00FF) >> 8);
                                            break;
                                            default:
                                            // 不会触发，但避免警告
                                            break;
                                    }
                                    cJSON *num = cJSON_CreateNumber((int32_t)value);
                                    if (!num) {
                                        ESP_LOGE(TAG, "Failed to create JSON number for registers %d-%d", j, j+1);
                                        success = false;
                                    } else {
                                        cJSON_AddItemToArray(group_data, num);
                                    }
                                }
                                break;
                            }

                            case PARSE_FLOAT_ABCD:
                            case PARSE_FLOAT_CDAB:
                            case PARSE_FLOAT_BADC:
                            case PARSE_FLOAT_DCBA: {
                                for (uint16_t j = 0; j < count && success; j += 2) {
                                    uint32_t raw;
                                    switch(method) {
                                        case PARSE_FLOAT_ABCD:
                                            raw = ((uint32_t)regs[j] << 16) | regs[j + 1];
                                            break;
                                        case PARSE_FLOAT_CDAB:
                                            raw = ((uint32_t)regs[j + 1] << 16) | regs[j];
                                            break;
                                        case PARSE_FLOAT_BADC:
                                            raw = ((uint32_t)(regs[j] & 0xFF00) << 8) |
                                                  ((uint32_t)(regs[j] & 0x00FF) << 24) |
                                                  ((uint32_t)(regs[j + 1] & 0xFF00) >> 8) |
                                                  ((uint32_t)(regs[j + 1] & 0x00FF) << 8);
                                            break;
                                        case PARSE_FLOAT_DCBA:
                                            raw = ((uint32_t)(regs[j + 1] & 0xFF00) >> 8) |
                                                  ((uint32_t)(regs[j + 1] & 0x00FF) << 8) |
                                                  ((uint32_t)(regs[j] & 0xFF00) >> 24) |
                                                  ((uint32_t)(regs[j] & 0x00FF) >> 8);
                                            break;
                                            default:
                                            // 不会触发，但避免警告
                                            break;
                                    }
                                    float value;
                                    memcpy(&value, &raw, sizeof(float));
                                    char number_buffer[32];
                                    snprintf(number_buffer, sizeof(number_buffer), "%.2f", value); // 保留两位小数
                                    cJSON *num = cJSON_CreateRaw(number_buffer);
                                    if (!num) {
                                        ESP_LOGE(TAG, "Failed to create JSON number for float registers %d-%d", j, j+1);
                                        success = false;
                                    } else {
                                        cJSON_AddItemToArray(group_data, num);
                                    }
                                }
                                break;
                            }
                        }
                        break;
                    }
                }

                if (success) {
                    // 使用 group_id 作为键，直接添加到 root 对象中
                    char group_key[16];
                    snprintf(group_key, sizeof(group_key), "group%d", group_id);
                    cJSON_AddItemToObject(root, group_key, group_data);
                    any_group_ready = true;
                    ESP_LOGI(TAG, "Successfully processed group %d", group_id);
                } else {
                    ESP_LOGE(TAG, "Failed to process group %d", group_id);
                    cJSON_Delete(group_data);
                }
            }

            if (any_group_ready) {
                if (cJSON_PrintPreallocated(root, json_buffer, JSON_BUFFER_SIZE, 0)) {
                    esp_mqtt_client_publish(mqtt_client,
                                          mqtt_config.topic,
                                          json_buffer,
                                          0, 0, 0);
                    ESP_LOGI(TAG, "Successfully published MQTT message");
                } else {
                    ESP_LOGE(TAG, "Failed to print JSON to buffer");
                }
            } else {
                ESP_LOGW(TAG, "No groups were ready for publishing");
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "MQTT not connected or disabled");
        }

delay_continue:
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(mqtt_config.publish_interval));
    }
}

// 初始化MQTT模块
esp_err_t mqtt_init(void)
{
    // Create MQTT publish task with increased stack size
    BaseType_t ret = xTaskCreate(mqtt_publish_task,
                                 "mqtt_publish",
                                 4096,
                                 NULL,
                                 6,
                                 &publish_task_handle);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create MQTT publish task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

// 启动MQTT客户端
esp_err_t mqtt_start(void)
{
    // 检查配置是否有效
    if (!mqtt_config.enabled || strlen(mqtt_config.broker_url) == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }
    // 配置MQTT客户端参数
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_config.broker_url,
        .credentials.username = mqtt_config.username,
        .credentials.authentication.password = mqtt_config.password,
    };
    // 初始化MQTT客户端
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    // 注册事件处理程序并启动客户端
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));

    return ESP_OK;
}

// 停止MQTT客户端
esp_err_t mqtt_stop(void)
{
    if (mqtt_client != NULL)
    {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    mqtt_connected = false;
    return ESP_OK;
}

// 更新MQTT配置
esp_err_t mqtt_update_config(const mqtt_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // 如果客户端正在运行，先停止它
    mqtt_stop();

    // 更新配置
    memcpy(&mqtt_config, config, sizeof(mqtt_config_t));

    // 如果启用了MQTT，重新启动客户端
    if (mqtt_config.enabled)
    {
        return mqtt_start();
    }

    return ESP_OK;
}

// 获取当前MQTT配置
esp_err_t mqtt_get_config(mqtt_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &mqtt_config, sizeof(mqtt_config_t));
    return ESP_OK;
}
// 获取MQTT连接状态
bool mqtt_is_connected(void)
{
    return mqtt_connected;
}

// 保存mqtt配置到NVS
esp_err_t save_mqtt_config(mqtt_config_t *config) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开 NVS handle
    err = nvs_open("mqtt_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // 保存字符串配置
    if ((err = nvs_set_str(nvs_handle, "broker_url", config->broker_url)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save broker_url: %s", esp_err_to_name(err));
        goto end;
    }
    if ((err = nvs_set_str(nvs_handle, "username", config->username)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save username: %s", esp_err_to_name(err));
        goto end;
    }
    if ((err = nvs_set_str(nvs_handle, "password", config->password)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        goto end;
    }
    if ((err = nvs_set_str(nvs_handle, "topic", config->topic)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save topic: %s", esp_err_to_name(err));
        goto end;
    }

    // 保存基本类型配置
    if ((err = nvs_set_u8(nvs_handle, "enabled", config->enabled)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save enabled: %s", esp_err_to_name(err));
        goto end;
    }
    if ((err = nvs_set_u8(nvs_handle, "group_count", config->group_count)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save group_count: %s", esp_err_to_name(err));
        goto end;
    }

    // 修改键名，避免超出 15 字符限制
    if ((err = nvs_set_u32(nvs_handle, "pub_intvl", config->publish_interval)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save pub_intvl: %s", esp_err_to_name(err));
        goto end;
    }

    // 保存数组配置
    ESP_LOGI(TAG, "Saving group_ids, size: %d", sizeof(config->group_ids));
    err = nvs_set_blob(nvs_handle, "group_ids", config->group_ids, sizeof(config->group_ids));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save group_ids: %s", esp_err_to_name(err));
        goto end;
    }

    ESP_LOGI(TAG, "Saving parse_methods, size: %d", sizeof(config->parse_methods));
    err = nvs_set_blob(nvs_handle, "parse_methods", config->parse_methods, sizeof(config->parse_methods));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save parse_methods: %s", esp_err_to_name(err));
        goto end;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit error: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "MQTT config saved successfully.");
    }

end:
    nvs_close(nvs_handle);
    return err;
}


// 从NVS中加载modbus配置
esp_err_t load_mqtt_config(mqtt_config_t *config) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开 NVS handle
    err = nvs_open("mqtt_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // 读取字符串配置
    size_t required_size;
    
    // 获取字符串长度并读取
    err = nvs_get_str(nvs_handle, "broker_url", NULL, &required_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, "broker_url", config->broker_url, &required_size);
        if (err != ESP_OK) goto end;
    }
    
    err = nvs_get_str(nvs_handle, "username", NULL, &required_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, "username", config->username, &required_size);
        if (err != ESP_OK) goto end;
    }
    
    err = nvs_get_str(nvs_handle, "password", NULL, &required_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, "password", config->password, &required_size);
        if (err != ESP_OK) goto end;
    }
    
    err = nvs_get_str(nvs_handle, "topic", NULL, &required_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, "topic", config->topic, &required_size);
        if (err != ESP_OK) goto end;
    }

    // 读取基本类型配置
    uint8_t enabled;
    err = nvs_get_u8(nvs_handle, "enabled", &enabled);
    if (err == ESP_OK) {
        config->enabled = enabled;
    }
    
    err = nvs_get_u8(nvs_handle, "group_count", &config->group_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto end;
    
    err = nvs_get_u32(nvs_handle, "pub_intvl", &config->publish_interval);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(TAG, "Failed to load pub_intvl: %s", esp_err_to_name(err));
    goto end;
    }

    // 读取数组配置
    size_t blob_size;
    
    blob_size = sizeof(config->group_ids);
    err = nvs_get_blob(nvs_handle, "group_ids", config->group_ids, &blob_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto end;
    
    blob_size = sizeof(config->parse_methods);
    err = nvs_get_blob(nvs_handle, "parse_methods", config->parse_methods, &blob_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto end;

    err = ESP_OK;

end:
    nvs_close(nvs_handle);
    return err;
}