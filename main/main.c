#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "modbus_config.h"
#include "modbus_task.h"
#include "web_server.h"
#include "simple_wifi_sta.h"
#include "uart_rtu.h"
#include "mqtt.h" 
#include "tcp_server.h"
#include "tcp_slave_regs.h"

// 日志标签
static const char* TAG = "main";

// 定义 WiFi 连接事件位
#define WIFI_CONNECT_BIT BIT0
static EventGroupHandle_t s_wifi_ev = NULL;

void wifi_event_handler(WIFI_EV_e ev) {
    // 当 WiFi 连接成功时，设置事件位
    if(ev == WIFI_CONNECTED) {
        xEventGroupSetBits(s_wifi_ev, WIFI_CONNECT_BIT);
    }
}

void print_memory_info() {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "内存信息:");
    ESP_LOGI(TAG, "总可用字节数: %d", info.total_free_bytes);
    ESP_LOGI(TAG, "总分配字节数: %d", info.total_allocated_bytes);
    ESP_LOGI(TAG, "最大空闲块: %d", info.largest_free_block);
    ESP_LOGI(TAG, "最小可用字节数: %d", info.minimum_free_bytes);
    UBaseType_t stackWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "堆栈高水位标记: %d 字节", stackWaterMark * sizeof(StackType_t));
}

void app_main(void) {
    // 初始化 NVS（非易失性存储）
    esp_err_t ret = nvs_flash_init();
    // 如果 NVS 分区已满或版本不匹配，则擦除重新初始化
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
    // 在NVS初始化完成后立即加载配置
    //加载modbusrtu主站配置
    ESP_LOGI(TAG, "正在从NVS加载配置...");
    ret = load_modbus_config_from_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "加载配置失败，使用默认配置");
    }
    // 加载MQTT配置
    ESP_LOGI(TAG, "正在从NVS加载MQTT配置...");
    ret = load_mqtt_config(&mqtt_config);
    if (ret != ESP_OK) {
    ESP_LOGW(TAG, "加载MQTT配置失败使用默认配置");
    }
    // 加载TCPSLAVE配置
    ESP_LOGI(TAG, "正在从NVS加载TCPSLAVE配置...");
    ret = load_tcp_slave_config_from_nvs(&tcp_slave);
    if (ret != ESP_OK) {
    ESP_LOGW(TAG, "加载TCPSLAVE配置失败使用默认配置");
    }


    // 初始化串口
    ESP_ERROR_CHECK(uart_init());
    
    // 创建事件组用于 WiFi 连接同步
    s_wifi_ev = xEventGroupCreate();
    EventBits_t ev = 0;
    // 初始化 WiFi Station 模式
    wifi_sta_init(wifi_event_handler);
    
    // 等待 WiFi 连接成功
    ev = xEventGroupWaitBits(s_wifi_ev, WIFI_CONNECT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if(ev & WIFI_CONNECT_BIT)
    {
        // 启动HTTP服务器
        start_webserver();
        
        // 如果MQTT已配置且启用，则启动MQTT客户端
        if (mqtt_config.enabled && strlen(mqtt_config.broker_url) > 0) {
            // 初始化MQTT
            ESP_ERROR_CHECK(mqtt_init());
            ESP_LOGI(TAG, "Starting MQTT client");
            ret = mqtt_start();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start MQTT client: %d", ret);
            }
        }
        //启动modbus_rtu
        start_modbus();
        //启动modbus_tcp
        start_tcp_server();

    }
    
    while(1)
    {
        print_memory_info();
        vTaskDelay(pdMS_TO_TICKS(10000));     // 每10秒打印一次堆栈信息和堆内存信息
    }
}