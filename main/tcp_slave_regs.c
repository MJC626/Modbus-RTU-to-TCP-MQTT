#include "tcp_slave_regs.h"
#include "modbus_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

// 创建互斥量
SemaphoreHandle_t modbus_mutex = NULL;

// 默认 tcp_slave 配置
tcp_slave_t tcp_slave = {
     .enabled = false,        // 默认不启用
    .server_port = 502,       // Modbus TCP 默认端口
    .slave_address = 123,       // 默认从站地址
    
    // 寄存器映射配置
    .maps = {
        {MAP_HOLD_TO_HOLD, 0, 0, 0, 10},
        {MAP_INPUT_TO_INPUT, 1, 0, 10, 10},
        {MAP_COIL_TO_COIL, 2, 0, 0, 10},
        {MAP_DISC_TO_DISC, 3, 0, 10, 10},
    },
    
    // 寄存器空间尺寸配置
    .reg_sizes = {
        .tab_bits_size = 50,
        .tab_input_bits_size = 50,
        .tab_registers_size = 50,
        .tab_input_registers_size = 50,
    }
};

// 定义tcp从站寄存器
static uint8_t *tab_bits = NULL;
static uint8_t *tab_input_bits = NULL;
static uint16_t *tab_registers = NULL;
static uint16_t *tab_input_registers = NULL;

// 更新从站数据函数
void update_slave_data(void) {
    xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    
    for (int i = 0; i < MAX_MAPS; i++) {
        if (!modbus_data.register_ready[tcp_slave.maps[i].group_index]) {
            continue;
        }

        switch (tcp_slave.maps[i].type) {
            case MAP_COIL_TO_COIL:
                for (uint16_t j = 0; j < tcp_slave.maps[i].count; j++) {
                    uint8_t byte_index = (tcp_slave.maps[i].master_start_addr + j) / 8;
                    uint8_t bit_index = (tcp_slave.maps[i].master_start_addr + j) % 8;
                    bool bit_value = (modbus_data.coils[tcp_slave.maps[i].group_index][byte_index] >> bit_index) & 0x01;
                    tab_bits[tcp_slave.maps[i].slave_start_addr + j] = bit_value;
                }
                break;
            
            case MAP_DISC_TO_DISC:
                for (uint16_t j = 0; j < tcp_slave.maps[i].count; j++) {
                    uint8_t byte_index = (tcp_slave.maps[i].master_start_addr + j) / 8;
                    uint8_t bit_index = (tcp_slave.maps[i].master_start_addr + j) % 8;
                    bool bit_value = (modbus_data.discrete_inputs[tcp_slave.maps[i].group_index][byte_index] >> bit_index) & 0x01;
                    tab_input_bits[tcp_slave.maps[i].slave_start_addr + j] = bit_value;
                }
                break;
            
            case MAP_HOLD_TO_HOLD:
                memcpy(&tab_registers[tcp_slave.maps[i].slave_start_addr],
                       &modbus_data.holding_regs[tcp_slave.maps[i].group_index][tcp_slave.maps[i].master_start_addr],
                       tcp_slave.maps[i].count * sizeof(uint16_t));
                break;
            
            case MAP_INPUT_TO_INPUT:
                memcpy(&tab_input_registers[tcp_slave.maps[i].slave_start_addr],
                       &modbus_data.input_regs[tcp_slave.maps[i].group_index][tcp_slave.maps[i].master_start_addr],
                       tcp_slave.maps[i].count * sizeof(uint16_t));
                break;
        }
    }
    
    xSemaphoreGive(modbus_mutex);
}

// 线圈(Coils)处理函数
static int get_bits_buf(void *buf, int bufsz) {
    if (bufsz < tcp_slave.reg_sizes.tab_bits_size) return -1;
    xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    memcpy(buf, tab_bits, tcp_slave.reg_sizes.tab_bits_size);
    xSemaphoreGive(modbus_mutex);
    return 0;
}

static int set_bits_buf(int index, int len, void *buf, int bufsz) {
    if (index + len > tcp_slave.reg_sizes.tab_bits_size) return -1;
    xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    memcpy(&tab_bits[index], &((uint8_t *)buf)[index], len);
    xSemaphoreGive(modbus_mutex);
    return 0;
}

// 离散输入(Discrete Inputs)处理函数
static int get_input_bits_buf(void *buf, int bufsz) {
    if (bufsz < tcp_slave.reg_sizes.tab_input_bits_size) return -1;
    xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    memcpy(buf, tab_input_bits, tcp_slave.reg_sizes.tab_input_bits_size);
    xSemaphoreGive(modbus_mutex);
    return 0;
}

// 保持寄存器(Holding Registers)处理函数
static int get_registers_buf(void *buf, int bufsz) {
    if (bufsz < tcp_slave.reg_sizes.tab_registers_size * sizeof(uint16_t)) return -1;
    xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    memcpy(buf, tab_registers, tcp_slave.reg_sizes.tab_registers_size * sizeof(uint16_t));
    xSemaphoreGive(modbus_mutex);
    return 0;
}

static int set_registers_buf(int index, int len, void *buf, int bufsz) {
    if (index + len > tcp_slave.reg_sizes.tab_registers_size) return -1;
    xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    memcpy(&tab_registers[index], &((uint16_t *)buf)[index], len * sizeof(uint16_t));
    xSemaphoreGive(modbus_mutex);
    return 0;
}

// 输入寄存器(Input Registers)处理函数
static int get_input_registers_buf(void *buf, int bufsz) {
    if (bufsz < tcp_slave.reg_sizes.tab_input_registers_size * sizeof(uint16_t)) return -1;
    xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    memcpy(buf, tab_input_registers, tcp_slave.reg_sizes.tab_input_registers_size * sizeof(uint16_t));
    xSemaphoreGive(modbus_mutex);
    return 0;
}

// 从站寄存器映射表配置（全局变量，但不初始化）
agile_modbus_slave_util_map_t bit_maps[1];
agile_modbus_slave_util_map_t input_bit_maps[1];
agile_modbus_slave_util_map_t register_maps[1];
agile_modbus_slave_util_map_t input_register_maps[1];

// 初始化互斥量和从站寄存器
void init_tcp_slave_regs(void) {
    modbus_mutex = xSemaphoreCreateMutex();
    if (modbus_mutex == NULL) {
        ESP_LOGE("MODBUS", "Failed to create mutex");
        return;
    }
    // 动态分配从站寄存器数组
    tab_bits = (uint8_t *)malloc(tcp_slave.reg_sizes.tab_bits_size);
    tab_input_bits = (uint8_t *)malloc(tcp_slave.reg_sizes.tab_input_bits_size);
    tab_registers = (uint16_t *)malloc(tcp_slave.reg_sizes.tab_registers_size * sizeof(uint16_t));
    tab_input_registers = (uint16_t *)malloc(tcp_slave.reg_sizes.tab_input_registers_size * sizeof(uint16_t));
    // 初始化从站寄存器数组
    memset(tab_bits, 0, tcp_slave.reg_sizes.tab_bits_size);
    memset(tab_input_bits, 0, tcp_slave.reg_sizes.tab_input_bits_size);
    memset(tab_registers, 0, tcp_slave.reg_sizes.tab_registers_size * sizeof(uint16_t));
    memset(tab_input_registers, 0, tcp_slave.reg_sizes.tab_input_registers_size * sizeof(uint16_t));
    // 初始化从站寄存器映射表
    bit_maps[0] = (agile_modbus_slave_util_map_t){
        0x0000,
        tcp_slave.reg_sizes.tab_bits_size - 1,
        get_bits_buf,
        set_bits_buf
    };
    
    input_bit_maps[0] = (agile_modbus_slave_util_map_t){
        0x0000,
        tcp_slave.reg_sizes.tab_input_bits_size - 1,
        get_input_bits_buf,
        NULL
    };
    
    register_maps[0] = (agile_modbus_slave_util_map_t){
        0x0000,
        tcp_slave.reg_sizes.tab_registers_size - 1,
        get_registers_buf,
        set_registers_buf
    };
    
    input_register_maps[0] = (agile_modbus_slave_util_map_t){
        0x0000,
        tcp_slave.reg_sizes.tab_input_registers_size - 1,
        get_input_registers_buf,
        NULL
    };
}

// 定时更新函数
void modbus_regs_update_task(void *pvParameters) {
    while (1) {
        update_slave_data();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//保存tcpslave配置函数
esp_err_t save_tcp_slave_config_to_nvs(tcp_slave_t *config) {
    nvs_handle_t handle;
    esp_err_t err;

    // 打开NVS存储
    err = nvs_open("tcp_slave", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    // 保存基础配置(使能状态、服务器端口、从站地址)
    struct {
        bool enabled;
        uint16_t server_port;
        uint8_t slave_address;
    } basic_config = {
        .enabled = config->enabled,
        .server_port = config->server_port,
        .slave_address = config->slave_address
    };
    
    err = nvs_set_blob(handle, "basic", &basic_config, sizeof(basic_config));
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // 保存寄存器映射配置
    err = nvs_set_blob(handle, "maps", config->maps, sizeof(config->maps));
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // 保存寄存器空间大小配置
    err = nvs_set_blob(handle, "reg_sizes", &config->reg_sizes, sizeof(config->reg_sizes));
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // 提交更改到NVS
    err = nvs_commit(handle);
    nvs_close(handle);
    
    return err;
}

//读取tcpslave配置函数
esp_err_t load_tcp_slave_config_from_nvs(tcp_slave_t *config) {
    nvs_handle_t handle;
    esp_err_t err;

    // 打开NVS存储
    err = nvs_open("tcp_slave", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    // 加载基础配置
    struct {
        bool enabled;
        uint16_t server_port;
        uint8_t slave_address;
    } basic_config;
    
    size_t required_size = sizeof(basic_config);
    err = nvs_get_blob(handle, "basic", &basic_config, &required_size);
    if (err == ESP_OK) {
        config->enabled = basic_config.enabled;
        config->server_port = basic_config.server_port;
        config->slave_address = basic_config.slave_address;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    // 加载寄存器映射配置
    required_size = sizeof(config->maps);
    err = nvs_get_blob(handle, "maps", config->maps, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    // 加载寄存器空间大小配置
    required_size = sizeof(config->reg_sizes);
    err = nvs_get_blob(handle, "reg_sizes", &config->reg_sizes, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    return ESP_OK;
}