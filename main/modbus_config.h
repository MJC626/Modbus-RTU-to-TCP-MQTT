#ifndef MODBUS_CONFIG_H
#define MODBUS_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MAX_REGS 100       //一次轮询读取最大寄存器个数
#define MAX_BITS 2048   // 最大位数
#define MAX_POLL_GROUPS 10 //最大轮询组

// 轮询组配置结构体
typedef struct {
    bool enabled;
    uint8_t slave_addr;
    uint8_t function_code;
    uint16_t start_addr;
    uint16_t reg_count;
    uint8_t uart_port;
} poll_group_config_t;

// Modbus总体配置结构体
typedef struct {
    uint32_t poll_interval;
    uint8_t group_count;
    poll_group_config_t groups[MAX_POLL_GROUPS];
} modbus_config_t;

// Modbus数据存储结构体
typedef struct {
    uint8_t coils[MAX_POLL_GROUPS][MAX_BITS/8];         // 功能码01 - 线圈状态
    uint8_t discrete_inputs[MAX_POLL_GROUPS][MAX_BITS/8];// 功能码02 - 离散输入状态
    uint16_t holding_regs[MAX_POLL_GROUPS][MAX_REGS];   // 功能码03 - 保持寄存器
    uint16_t input_regs[MAX_POLL_GROUPS][MAX_REGS];     // 功能码04 - 输入寄存器
    bool register_ready[MAX_POLL_GROUPS];               // 数据就绪标志
} modbus_data_t;

// 外部变量声明
extern modbus_config_t modbus_config;
extern modbus_data_t modbus_data;

esp_err_t save_modbus_config_to_nvs(void);
esp_err_t load_modbus_config_from_nvs(void);

#endif