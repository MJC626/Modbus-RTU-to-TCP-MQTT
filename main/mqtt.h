#ifndef _MQTT_H_
#define _MQTT_H_

#include <stdint.h>
#include "esp_err.h"
#include "mqtt_client.h"

#define MAX_POLL_GROUPS 10

// 数据解析方式枚举
typedef enum {
    PARSE_INT16_SIGNED,    // 有符号16位整数
    PARSE_INT16_UNSIGNED,  // 无符号16位整数
    PARSE_INT32_ABCD,      // 32位整数 - ABCD顺序
    PARSE_INT32_CDAB,      // 32位整数 - CDAB顺序
    PARSE_INT32_BADC,      // 32位整数 - BADC顺序
    PARSE_INT32_DCBA,      // 32位整数 - DCBA顺序
    PARSE_FLOAT_ABCD,      // 32位浮点 - ABCD顺序
    PARSE_FLOAT_CDAB,      // 32位浮点 - CDAB顺序
    PARSE_FLOAT_BADC,      // 32位浮点 - BADC顺序
    PARSE_FLOAT_DCBA       // 32位浮点 - DCBA顺序
} parse_method_t;

// MQTT配置结构体
typedef struct {
    char broker_url[128];
    char username[32];
    char password[32];
    char topic[64];
    bool enabled;
    uint8_t group_ids[MAX_POLL_GROUPS];
    uint8_t group_count;
    uint32_t publish_interval;
    parse_method_t parse_methods[MAX_POLL_GROUPS];  // 每组的解析方式
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

esp_err_t load_mqtt_config(mqtt_config_t *config);
esp_err_t save_mqtt_config(mqtt_config_t *config);

// 外部MQTT配置变量声明
extern mqtt_config_t mqtt_config;

#endif // _MQTT_H_