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


// 任务信息打印
void print_task_info(void) {
    char buffer[1024];  // 存储任务信息

    // 打印任务列表（状态、优先级、剩余栈空间等）
    memset(buffer, 0, sizeof(buffer));
    vTaskList(buffer);
    ESP_LOGI(TAG, "Task List:\nName\t\tState\tPrio\tStack\tTask#\n%s", buffer);

    // 打印任务运行时间统计（CPU 占用率）
    memset(buffer, 0, sizeof(buffer));
    vTaskGetRunTimeStats(buffer);
    ESP_LOGI(TAG, "Task Runtime Stats:\nName\t\tAbs Time\t%% Time\n%s", buffer);
}

// 任务监视器（每 5 秒打印一次任务信息）
void task_monitor(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 每 10 秒打印一次
        print_task_info();
    }
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
    //加载串口配置
    ESP_LOGI(TAG, "正在从NVS加载串口配置...");
    ret = load_uart_params_from_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "加载串口配置失败，使用默认配置");
    }

    //加载modbusrtu主站配置
    ESP_LOGI(TAG, "正在从NVS加载Modbus配置...");
    ret = load_modbus_config_from_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "加载Modbus配置失败，使用默认配置");
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
        if(tcp_slave.enabled){
            //启动modbus_tcp
            ESP_LOGI(TAG, "Starting Modbus tcp");
            start_tcp_server();
        }

            // 创建任务监视器
    xTaskCreate(task_monitor, "TaskMonitor", 4096, NULL, 5, NULL);

    }
    
}