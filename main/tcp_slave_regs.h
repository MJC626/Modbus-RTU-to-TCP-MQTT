#ifndef TCP_SLAVE_REGS_H
#define TCP_SLAVE_REGS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "agile_modbus.h"
#include "agile_modbus_slave_util.h"

// 声明初始化函数
void init_tcp_slave_regs(void);

void update_slave_data(void);

void modbus_regs_update_task(void *pvParameters);


// 声明互斥量
extern SemaphoreHandle_t modbus_mutex;

// 声明寄存器映射表
extern agile_modbus_slave_util_map_t bit_maps[];
extern agile_modbus_slave_util_map_t input_bit_maps[];
extern agile_modbus_slave_util_map_t register_maps[];
extern agile_modbus_slave_util_map_t input_register_maps[];

// 最大映射组数
#define MAX_MAPS 10

// 映射类型枚举
typedef enum {
    MAP_COIL_TO_COIL,
    MAP_DISC_TO_DISC,
    MAP_HOLD_TO_HOLD,
    MAP_INPUT_TO_INPUT
} map_type_t;

// tcp_slave配置结构体
typedef struct {
    bool enabled;            // 是否启用

    // 基础通信参数
    uint16_t server_port;      // Modbus TCP 端口号
    uint8_t slave_address;     // 从站设备地址
    
    // 寄存器映射配置
    struct {
        map_type_t type;
        uint8_t group_index;
        uint16_t master_start_addr;
        uint16_t slave_start_addr;
        uint16_t count;
    } maps[MAX_MAPS];
    
    // 寄存器空间尺寸配置
    struct {
        uint16_t tab_bits_size;
        uint16_t tab_input_bits_size;
        uint16_t tab_registers_size;
        uint16_t tab_input_registers_size;
    } reg_sizes;
} tcp_slave_t;

//声明默认 tcp_slave 配置
extern tcp_slave_t tcp_slave; 


esp_err_t save_tcp_slave_config_to_nvs(tcp_slave_t *config);
esp_err_t load_tcp_slave_config_from_nvs(tcp_slave_t *config);


#endif
