#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_
#include "esp_err.h"

//定义一个枚举
typedef enum
{
    WIFI_DISCONNECTED,      //wifi断开
    WIFI_CONNECTED,         //wifi已连接
}WIFI_EV_e;

typedef void(*wifi_event_cb)(WIFI_EV_e);

//WIFI STA初始化
esp_err_t wifi_sta_init(wifi_event_cb f);
//wifi配置NVS存储
esp_err_t wifi_save_config(const char* ssid, const char* password);
esp_err_t wifi_load_config(void);

#endif
