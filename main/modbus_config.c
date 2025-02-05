#include "modbus_config.h"
#include "nvs_flash.h"
#include "esp_log.h"

// 日志标签
static const char* TAG = "modbus_config";

// 定义modbus默认配置
modbus_config_t modbus_config = {
    .poll_interval = 1000,
    .group_count = 1,
    .groups = {
        {.enabled = true, .slave_addr = 1, .function_code = 1, .start_addr = 0, .reg_count = 10, .uart_port = 1},
    }
};

modbus_data_t modbus_data = {0};

// 保存配置到NVS
esp_err_t save_modbus_config_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开NVS命名空间
    err = nvs_open("modbus_cfg", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // 保存基本配置
    err = nvs_set_u32(nvs_handle, "poll_interval", modbus_config.poll_interval);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving poll_interval: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_u8(nvs_handle, "group_count", modbus_config.group_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving group_count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // 保存每个轮询组的配置
    for (int i = 0; i < modbus_config.group_count; i++) {
        char key[32];
        
        // 为每个字段创建唯一的键名
        snprintf(key, sizeof(key), "group%d_enabled", i);
        err = nvs_set_u8(nvs_handle, key, modbus_config.groups[i].enabled);
        
        snprintf(key, sizeof(key), "group%d_slave", i);
        err |= nvs_set_u8(nvs_handle, key, modbus_config.groups[i].slave_addr);
        
        snprintf(key, sizeof(key), "group%d_func", i);
        err |= nvs_set_u8(nvs_handle, key, modbus_config.groups[i].function_code);
        
        snprintf(key, sizeof(key), "group%d_start", i);
        err |= nvs_set_u16(nvs_handle, key, modbus_config.groups[i].start_addr);
        
        snprintf(key, sizeof(key), "group%d_count", i);
        err |= nvs_set_u16(nvs_handle, key, modbus_config.groups[i].reg_count);
        
        snprintf(key, sizeof(key), "group%d_uart", i);
        err |= nvs_set_u8(nvs_handle, key, modbus_config.groups[i].uart_port);
        
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error saving group %d config: %s", i, esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

// 从NVS加载配置
esp_err_t load_modbus_config_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开NVS命名空间
    err = nvs_open("modbus_cfg", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved configuration found, using defaults");
        return err;
    }

    // 加载基本配置
    uint32_t poll_interval;
    err = nvs_get_u32(nvs_handle, "poll_interval", &poll_interval);
    if (err == ESP_OK) {
        modbus_config.poll_interval = poll_interval;
    }

    uint8_t group_count;
    err = nvs_get_u8(nvs_handle, "group_count", &group_count);
    if (err == ESP_OK && group_count <= MAX_POLL_GROUPS) {
        modbus_config.group_count = group_count;
    }

    // 加载每个轮询组的配置
    for (int i = 0; i < modbus_config.group_count; i++) {
        char key[32];
        uint8_t u8_val;
        uint16_t u16_val;
        
        // 读取每个字段
        snprintf(key, sizeof(key), "group%d_enabled", i);
        if (nvs_get_u8(nvs_handle, key, &u8_val) == ESP_OK) {
            modbus_config.groups[i].enabled = u8_val;
        }
        
        snprintf(key, sizeof(key), "group%d_slave", i);
        if (nvs_get_u8(nvs_handle, key, &u8_val) == ESP_OK) {
            modbus_config.groups[i].slave_addr = u8_val;
        }
        
        snprintf(key, sizeof(key), "group%d_func", i);
        if (nvs_get_u8(nvs_handle, key, &u8_val) == ESP_OK) {
            modbus_config.groups[i].function_code = u8_val;
        }
        
        snprintf(key, sizeof(key), "group%d_start", i);
        if (nvs_get_u16(nvs_handle, key, &u16_val) == ESP_OK) {
            modbus_config.groups[i].start_addr = u16_val;
        }
        
        snprintf(key, sizeof(key), "group%d_count", i);
        if (nvs_get_u16(nvs_handle, key, &u16_val) == ESP_OK) {
            modbus_config.groups[i].reg_count = u16_val;
        }
        
        snprintf(key, sizeof(key), "group%d_uart", i);
        if (nvs_get_u8(nvs_handle, key, &u8_val) == ESP_OK) {
            modbus_config.groups[i].uart_port = u8_val;
        }
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}