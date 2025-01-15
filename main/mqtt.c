#include <string.h>
#include "mqtt.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "modbus_config.h"
#include "cJSON.h"

// 日志标签
static const char* TAG = "mqtt";

// 静态JSON缓冲区，避免堆栈上分配内存
#define JSON_BUFFER_SIZE 8192
static char json_buffer[JSON_BUFFER_SIZE];

// MQTT配置结构体的默认配置
mqtt_config_t mqtt_config = {
    .broker_url = "",      
    .username = "",        
    .password = "",        
    .topic = "modbus/data", //默认主题
    .enabled = false,       //默认不启用
    .group_ids = {0},       // 默认只发布第一组
    .group_count = 1,       // 默认发布1个组
    .publish_interval = 5000
};

// MQTT客户端句柄
static esp_mqtt_client_handle_t mqtt_client = NULL;
// MQTT连接状态标志
static bool mqtt_connected = false;
// 发布任务句柄
static TaskHandle_t publish_task_handle = NULL;

// MQTT事件处理函数
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    switch (event->event_id) {
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
    
    while (1) {
        if (mqtt_connected && mqtt_config.enabled) {
            // 创建根JSON对象
            cJSON *root = cJSON_CreateObject();
            if (!root) {
                ESP_LOGE(TAG, "Failed to create JSON object");
                goto delay_continue;
            }
            cJSON *groups = cJSON_CreateArray();
            if (!groups) {
                ESP_LOGE(TAG, "Failed to create groups array");
                cJSON_Delete(root);
                goto delay_continue;
            }
            
            bool any_group_ready = false;
            
            // 遍历配置的所有组
            for (int i = 0; i < mqtt_config.group_count; i++) {
                uint8_t group_id = mqtt_config.group_ids[i];
                
                // 检查组ID是否有效且数据已准备好
                if (group_id >= modbus_config.group_count || 
                    !modbus_data.register_ready[group_id]) {
                    continue;
                }
                
                // 创建组对象
                cJSON *group = cJSON_CreateObject();
                if (!group) {
                    continue;
                }
                
                // 添加组ID和功能码
                cJSON_AddNumberToObject(group, "group_id", group_id);
                cJSON_AddNumberToObject(group, "function_code", 
                    modbus_config.groups[group_id].function_code);
                
                // 创建数据数组
                cJSON *data = cJSON_CreateArray();
                if (!data) {
                    cJSON_Delete(group);
                    continue;
                }
                
                // 根据功能码选择数据源并添加到JSON中
                bool success = true;
                uint8_t function_code = modbus_config.groups[group_id].function_code;
                uint16_t count = modbus_config.groups[group_id].reg_count;

                switch(function_code) {
                    case 1: { // 线圈状态
                        // 对于位数据，需要逐位解析并添加
                        for (uint16_t j = 0; j < count && success; j++) {
                            uint8_t byte_index = j / 8;
                            uint8_t bit_index = j % 8;
                            bool bit_value = (modbus_data.coils[group_id][byte_index] >> bit_index) & 0x01;
                            cJSON *num = cJSON_CreateNumber(bit_value);
                            if (!num) {
                                success = false;
                            } else {
                                cJSON_AddItemToArray(data, num);
                            }
                        }
                        break;
                    }
                    case 2: { // 离散输入状态
                        for (uint16_t j = 0; j < count && success; j++) {
                            uint8_t byte_index = j / 8;
                            uint8_t bit_index = j % 8;
                            bool bit_value = (modbus_data.discrete_inputs[group_id][byte_index] >> bit_index) & 0x01;
                            cJSON *num = cJSON_CreateNumber(bit_value);
                            if (!num) {
                                success = false;
                            } else {
                                cJSON_AddItemToArray(data, num);
                            }
                        }
                        break;
                    }
                    case 3: { // 保持寄存器
                        for (uint16_t j = 0; j < count && success; j++) {
                            cJSON *num = cJSON_CreateNumber(modbus_data.holding_regs[group_id][j]);
                            if (!num) {
                                success = false;
                            } else {
                                cJSON_AddItemToArray(data, num);
                            }
                        }
                        break;
                    }
                    case 4: { // 输入寄存器
                        for (uint16_t j = 0; j < count && success; j++) {
                            cJSON *num = cJSON_CreateNumber(modbus_data.input_regs[group_id][j]);
                            if (!num) {
                                success = false;
                            } else {
                                cJSON_AddItemToArray(data, num);
                            }
                        }
                        break;
                    }
                    default:
                        ESP_LOGE(TAG, "组 %d 不支持的功能码: %d", group_id, function_code);
                        success = false;
                        break;
                }
                
                if (success) {
                    cJSON_AddItemToObject(group, "data", data);
                    cJSON_AddItemToArray(groups, group);
                    any_group_ready = true;
                } else {
                    cJSON_Delete(data);
                    cJSON_Delete(group);
                }
            }
            
            if (any_group_ready) {
                cJSON_AddItemToObject(root, "groups", groups);
                
                // 将JSON写入静态缓冲区
                if (cJSON_PrintPreallocated(root, json_buffer, JSON_BUFFER_SIZE, 0)) {
                    esp_mqtt_client_publish(mqtt_client, 
                                         mqtt_config.topic,
                                         json_buffer,
                                         0, 0, 0);
                    ESP_LOGI(TAG, "MQTT 发布成功");
                } else {
                    ESP_LOGE(TAG, "Failed to print JSON to buffer");
                }
            }
            cJSON_Delete(root);
        }
        
        delay_continue:
        // 按照配置的发布间隔延时
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(mqtt_config.publish_interval));
    }
}

// 初始化MQTT模块
esp_err_t mqtt_init(void) {
    // Create MQTT publish task with increased stack size
    BaseType_t ret = xTaskCreate(mqtt_publish_task,
                                "mqtt_publish",
                                4096,
                                NULL,
                                3,
                                &publish_task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT publish task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

// 启动MQTT客户端
esp_err_t mqtt_start(void) {
    // 检查配置是否有效
    if (!mqtt_config.enabled || strlen(mqtt_config.broker_url) == 0) {
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
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    // 注册事件处理程序并启动客户端
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));

    return ESP_OK;
}

// 停止MQTT客户端
esp_err_t mqtt_stop(void) {
    if (mqtt_client != NULL) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    mqtt_connected = false;
    return ESP_OK;
}

// 更新MQTT配置
esp_err_t mqtt_update_config(const mqtt_config_t* config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 如果客户端正在运行，先停止它
    mqtt_stop();

    // 更新配置
    memcpy(&mqtt_config, config, sizeof(mqtt_config_t));

    // 如果启用了MQTT，重新启动客户端
    if (mqtt_config.enabled) {
        return mqtt_start();
    }

    return ESP_OK;
}

// 获取当前MQTT配置
esp_err_t mqtt_get_config(mqtt_config_t* config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(config, &mqtt_config, sizeof(mqtt_config_t));
    return ESP_OK;
}
// 获取MQTT连接状态
bool mqtt_is_connected(void) {
    return mqtt_connected;
}