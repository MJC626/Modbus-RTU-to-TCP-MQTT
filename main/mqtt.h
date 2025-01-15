#ifndef _MQTT_H_
#define _MQTT_H_

#include <stdint.h>
#include "esp_err.h"
#include "mqtt_client.h"

#define MAX_POLL_GROUPS 10

// MQTT配置结构体
typedef struct {
    char broker_url[128];                //mqtt服务器
    char username[32];                   //用户名
    char password[32];                   //密码
    char topic[64];                      //发布主题
    bool enabled;                        //是否启用
    uint8_t group_ids[MAX_POLL_GROUPS];  // 要发布的组ID数组
    uint8_t group_count;                 // 要发布的组数量
    uint32_t publish_interval;
} mqtt_config_t;

// 初始化MQTT模块
esp_err_t mqtt_init(void);

// 启动MQTT客户端
esp_err_t mqtt_start(void);

// 停止MQTT客户端
esp_err_t mqtt_stop(void);

// 更新MQTT配置
esp_err_t mqtt_update_config(const mqtt_config_t* config);

// 获取当前MQTT配置
esp_err_t mqtt_get_config(mqtt_config_t* config);

// 检查MQTT连接状态
bool mqtt_is_connected(void);

// 外部MQTT配置变量声明
extern mqtt_config_t mqtt_config;

#endif // _MQTT_H_