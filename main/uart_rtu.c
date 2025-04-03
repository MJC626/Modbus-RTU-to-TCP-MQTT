#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "uart_rtu.h"
#include "nvs_flash.h"
#include "nvs.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// 定义缓冲区大小，使用串口缓冲区的默认大小
#define BUF_SIZE UART_BUFFER_SIZE

// 定义固定的串口引脚
#define UART0_TX_PIN 6
#define UART0_RX_PIN 7
#define UART1_TX_PIN 17
#define UART1_RX_PIN 18
#define UART2_TX_PIN 15
#define UART2_RX_PIN 16

// 定义3个串口的参数数组
uart_param_t uart_params[3] = {
    {
        .baud_rate = BAUD_115200,
        .data_bits = DATA_BITS_8,
        .parity = PARITY_NONE,
        .stop_bits = STOP_BITS_1
    },
    {
        .baud_rate = BAUD_115200,
        .data_bits = DATA_BITS_8,
        .parity = PARITY_NONE,
        .stop_bits = STOP_BITS_1
    },
    {
        .baud_rate = BAUD_115200,
        .data_bits = DATA_BITS_8,
        .parity = PARITY_NONE,
        .stop_bits = STOP_BITS_1
    }
};

// 声明UART0、UART1和UART2的事件队列句柄
static QueueHandle_t uart0_queue;
static QueueHandle_t uart1_queue;
static QueueHandle_t uart2_queue;
// 声明用于接收数据同步的信号量句柄
static SemaphoreHandle_t rx0_sem;
static SemaphoreHandle_t rx1_sem;
static SemaphoreHandle_t rx2_sem;

//UART0发送数据
int send_data0(uint8_t *buf, int len) {
    return uart_write_bytes(UART_NUM_0, (const char*)buf, len);
}

//UART1发送数据
int send_data1(uint8_t *buf, int len) {
    return uart_write_bytes(UART_NUM_1, (const char*)buf, len);
}

//UART2发送数据
int send_data2(uint8_t *buf, int len) {
    return uart_write_bytes(UART_NUM_2, (const char*)buf, len);
}

// 根据波特率获取字节超时时间（毫秒）
static int get_byte_timeout_by_baudrate(uart_rtu_baud_rate_t baud_rate) {
    switch (baud_rate) {
        case BAUD_9600:
            return 5;
        case BAUD_19200:
            return 3;
        case BAUD_38400:
            return 2;
        case BAUD_57600:
            return 2;
        case BAUD_115200:
            return 2;
        default:
            return 5;
    }
}

//UART0接收数据
int receive_data0(uint8_t *buf, int bufsz, int timeout) {
    int len = 0;
    int rc;
    TickType_t start = xTaskGetTickCount();
    TickType_t last_byte_time = start;
    int t35_timeout = get_byte_timeout_by_baudrate(uart_params[1].baud_rate);
    
    while (1) {
        int elapsed_time = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed_time >= timeout) {
            break;
        }
        
        int remaining_timeout = timeout - elapsed_time;
        
        if (xSemaphoreTake(rx0_sem, pdMS_TO_TICKS(MIN(remaining_timeout, 50))) == pdTRUE) {
            size_t available_bytes;
            uart_get_buffered_data_len(UART_NUM_0, &available_bytes);
            
            if (available_bytes > 0) {
                rc = uart_read_bytes(UART_NUM_0, buf + len, MIN(available_bytes, bufsz - len), pdMS_TO_TICKS(20));
                
                if (rc > 0) {
                    len += rc;
                    last_byte_time = xTaskGetTickCount();
                    
                    if (len >= bufsz) {
                        break;
                    }
                }
            }
        } else {
            if (len > 0) {
                int idle_time = (xTaskGetTickCount() - last_byte_time) * portTICK_PERIOD_MS;
                if (idle_time >= t35_timeout) {
                    break;
                }
            }
        }
        
        if (len > 0) {
            int idle_time = (xTaskGetTickCount() - last_byte_time) * portTICK_PERIOD_MS;
            if (idle_time >= t35_timeout) {
                break;
            }
        }
    }
    return len;
}

// UART1接收数据
int receive_data1(uint8_t *buf, int bufsz, int timeout) {
    int len = 0;
    int rc;
    TickType_t start = xTaskGetTickCount();
    TickType_t last_byte_time = start;
    int t35_timeout = get_byte_timeout_by_baudrate(uart_params[1].baud_rate);
    
    while (1) {
        // 计算已经过去的时间
        int elapsed_time = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed_time >= timeout) {
            break;
        }
        
        int remaining_timeout = timeout - elapsed_time;
        
        // 等待数据到达
        if (xSemaphoreTake(rx1_sem, pdMS_TO_TICKS(MIN(remaining_timeout, 50))) == pdTRUE) {
            // 检查有多少数据可用
            size_t available_bytes;
            uart_get_buffered_data_len(UART_NUM_1, &available_bytes);
            
            if (available_bytes > 0) {
                // 每次读取可用的数据，但不超过缓冲区剩余空间
                rc = uart_read_bytes(UART_NUM_1, buf + len, MIN(available_bytes, bufsz - len), pdMS_TO_TICKS(20));
                
                if (rc > 0) {
                    len += rc;
                    last_byte_time = xTaskGetTickCount();
                    
                    if (len >= bufsz) {
                        break; // 缓冲区已满
                    }
                }
            }
        } else {
            // 信号量等待超时，检查是否应该结束接收
            if (len > 0) {
                // 检查字符间隔时间
                int idle_time = (xTaskGetTickCount() - last_byte_time) * portTICK_PERIOD_MS;
                if (idle_time >= t35_timeout) {
                    break; // 帧结束
                }
            }
        }
        
        // 如果已经接收到数据，再次检查是否达到帧结束条件
        if (len > 0) {
            int idle_time = (xTaskGetTickCount() - last_byte_time) * portTICK_PERIOD_MS;
            if (idle_time >= t35_timeout) {
                break; // 帧结束
            }
        }
    }
    return len;
}

//UART2接收数据
int receive_data2(uint8_t *buf, int bufsz, int timeout) {
    int len = 0;
    int rc;
    TickType_t start = xTaskGetTickCount();
    TickType_t last_byte_time = start;
    int t35_timeout = get_byte_timeout_by_baudrate(uart_params[1].baud_rate);
    
    while (1) {
        int elapsed_time = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed_time >= timeout) {
            break;
        }
        
        int remaining_timeout = timeout - elapsed_time;
        
        if (xSemaphoreTake(rx2_sem, pdMS_TO_TICKS(MIN(remaining_timeout, 50))) == pdTRUE) {
            size_t available_bytes;
            uart_get_buffered_data_len(UART_NUM_2, &available_bytes);
            
            if (available_bytes > 0) {
                rc = uart_read_bytes(UART_NUM_2, buf + len, MIN(available_bytes, bufsz - len), pdMS_TO_TICKS(20));
                
                if (rc > 0) {
                    len += rc;
                    last_byte_time = xTaskGetTickCount();
                    
                    if (len >= bufsz) {
                        break;
                    }
                }
            }
        } else {
            if (len > 0) {
                int idle_time = (xTaskGetTickCount() - last_byte_time) * portTICK_PERIOD_MS;
                if (idle_time >= t35_timeout) {
                    break;
                }
            }
        }
        
        if (len > 0) {
            int idle_time = (xTaskGetTickCount() - last_byte_time) * portTICK_PERIOD_MS;
            if (idle_time >= t35_timeout) {
                break;
            }
        }
    }
    return len;
}


// UART0事件处理任务函数
static void uart0_event_task(void *pvParameters) {
    uart_event_t event;
    size_t buffered_size;
    
    while (1) {
        if (xQueueReceive(uart0_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    // 获取当前缓冲区数据长度
                    uart_get_buffered_data_len(UART_NUM_0, &buffered_size);
                    // 通知接收任务
                    xSemaphoreGive(rx0_sem);
                    break;
                
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(UART_NUM_0);
                    xQueueReset(uart0_queue);
                    break;
                
                default:
                    break;
            }
        }
    }
}
// UART1事件处理任务函数
static void uart1_event_task(void *pvParameters) {
    uart_event_t event;
    size_t buffered_size;
    
    while (1) {
        if (xQueueReceive(uart1_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    // 获取当前缓冲区数据长度
                    uart_get_buffered_data_len(UART_NUM_1, &buffered_size);
                    // 通知接收任务
                    xSemaphoreGive(rx1_sem);
                    break;
                
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(uart1_queue);
                    break;
                
                default:
                    break;
            }
        }
    }
}

static void uart2_event_task(void *pvParameters) {
    uart_event_t event;
    size_t buffered_size;
    
    while (1) {
        if (xQueueReceive(uart2_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    // 获取当前缓冲区数据长度
                    uart_get_buffered_data_len(UART_NUM_2, &buffered_size);
                    // 通知接收任务
                    xSemaphoreGive(rx2_sem);
                    break;
                
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(UART_NUM_2);
                    xQueueReset(uart2_queue);
                    break;
                
                default:
                    break;
            }
        }
    }
}
//初始化UART0、UART1和UART2
int uart_init(void) {
    // UART基本配置
    uart_config_t uart_config;
    
    // 初始化3个UART
    for(int i = 0; i < 3; i++) {
        uart_config.baud_rate = uart_params[i].baud_rate;
        uart_config.data_bits = uart_params[i].data_bits;
        uart_config.parity = uart_params[i].parity;
        uart_config.stop_bits = uart_params[i].stop_bits;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config.rx_flow_ctrl_thresh = 122;
        uart_config.source_clk = UART_SCLK_APB;
        
        ESP_ERROR_CHECK(uart_param_config(i, &uart_config));
        
        // 根据UART端口号设置对应的固定引脚
        if (i == 0) {
            ESP_ERROR_CHECK(uart_set_pin(i, UART0_TX_PIN, UART0_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        } else if (i == 1) {
            ESP_ERROR_CHECK(uart_set_pin(i, UART1_TX_PIN, UART1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        } else {
            ESP_ERROR_CHECK(uart_set_pin(i, UART2_TX_PIN, UART2_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        }
        
        ESP_ERROR_CHECK(uart_driver_install(i, BUF_SIZE * 2, BUF_SIZE * 2, 20, i == 0 ? &uart0_queue : (i == 1 ? &uart1_queue : &uart2_queue), ESP_INTR_FLAG_IRAM));
        uart_set_rx_timeout(i, 3);
        }
    
    rx0_sem = xSemaphoreCreateBinary();
    rx1_sem = xSemaphoreCreateBinary();
    rx2_sem = xSemaphoreCreateBinary();
    if (rx0_sem == NULL || rx1_sem == NULL || rx2_sem == NULL) {
        ESP_LOGE("UART", "Failed to create rx semaphores");
        return -1;
    }
    
    xTaskCreate(uart0_event_task, "uart0_event_task", 2048, NULL, 12, NULL);
    xTaskCreate(uart1_event_task, "uart1_event_task", 2048, NULL, 12, NULL);
    xTaskCreate(uart2_event_task, "uart2_event_task", 2048, NULL, 12, NULL);
    
    return ESP_OK;
}

// 从NVS中读取UART参数
esp_err_t load_uart_params_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开NVS命名空间
    err = nvs_open("uart_params", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("UART", "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // 为每个UART端口分别读取参数
    for(int i = 0; i < 3; i++) {
        char key[16];
        uint32_t value;
        
        // 读取波特率
        snprintf(key, sizeof(key), "baud_rate_%d", i);
        err = nvs_get_u32(nvs_handle, key, &value);
        if (err == ESP_OK) {
            uart_params[i].baud_rate = (uart_rtu_baud_rate_t)value;
        }
        
        // 读取数据位
        snprintf(key, sizeof(key), "data_bits_%d", i);
        err = nvs_get_u32(nvs_handle, key, &value);
        if (err == ESP_OK) {
            uart_params[i].data_bits = (uart_rtu_data_bits_t)value;
        }
        
        // 读取校验位
        snprintf(key, sizeof(key), "parity_%d", i);
        err = nvs_get_u32(nvs_handle, key, &value);
        if (err == ESP_OK) {
            uart_params[i].parity = (uart_rtu_parity_t)value;
        }
        
        // 读取停止位
        snprintf(key, sizeof(key), "stop_bits_%d", i);
        err = nvs_get_u32(nvs_handle, key, &value);
        if (err == ESP_OK) {
            uart_params[i].stop_bits = (uart_rtu_stop_bits_t)value;
        }
    }

    // 关闭NVS句柄
    nvs_close(nvs_handle);
    return ESP_OK;
}

// 保存UART参数到NVS
esp_err_t save_uart_params_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 打开NVS命名空间
    err = nvs_open("uart_params", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("UART", "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // 为每个UART端口分别保存参数
    for(int i = 0; i < 3; i++) {
        char key[16];
        
        // 保存波特率
        snprintf(key, sizeof(key), "baud_rate_%d", i);
        err = nvs_set_u32(nvs_handle, key, (uint32_t)uart_params[i].baud_rate);
        if (err != ESP_OK) continue;
        
        // 保存数据位
        snprintf(key, sizeof(key), "data_bits_%d", i);
        err = nvs_set_u32(nvs_handle, key, (uint32_t)uart_params[i].data_bits);
        if (err != ESP_OK) continue;
        
        // 保存校验位
        snprintf(key, sizeof(key), "parity_%d", i);
        err = nvs_set_u32(nvs_handle, key, (uint32_t)uart_params[i].parity);
        if (err != ESP_OK) continue;
        
        // 保存停止位
        snprintf(key, sizeof(key), "stop_bits_%d", i);
        err = nvs_set_u32(nvs_handle, key, (uint32_t)uart_params[i].stop_bits);
        if (err != ESP_OK) continue;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("UART", "Error committing NVS changes: %s", esp_err_to_name(err));
    }

    // 关闭NVS句柄
    nvs_close(nvs_handle);
    return err;
}